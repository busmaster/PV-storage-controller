#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <modbus/modbus.h>
#include <mosquitto.h>
#include <cjson/cJSON.h>
#include <sys/socket.h>

static int grid_power;
volatile sig_atomic_t keep_running = 1;

bool new_meter;
bool mosq_connected;

void handle_sigint(int sig) {
    keep_running = 0;
};

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {

    cJSON *root = cJSON_Parse((const char*)msg->payload);
    cJSON *power = cJSON_GetObjectItemCaseSensitive(root, "power");
    cJSON *p_plus = cJSON_GetObjectItemCaseSensitive(power, "P+");
    cJSON *p_minus = cJSON_GetObjectItemCaseSensitive(power, "P-");
    if (cJSON_IsNumber(p_plus) && cJSON_IsNumber(p_minus)) {
        grid_power = p_plus->valueint - p_minus->valueint;
    }
    cJSON_Delete(root);
    new_meter = true;
}

void on_connect(struct mosquitto *mq, void *obj, int result) {

    mosq_connected = true;
}

void on_disconnect(struct mosquitto *mq, void *obj, int rc) {

    mosq_connected = false;
}

int main(int argc, char *argv[]) {
    modbus_t *ctx;
    struct mosquitto *mosq;
    int mosq_fd;
    fd_set rfds;
    struct timeval   tv;
    int ret;
    int storage_power = 0;
    int limit;
    int delay = 0;
    bool charge_only = false;
    uint16_t force_mode;
    uint16_t force_charge_power;
    uint16_t force_discharge_power;
    bool modbus_connected = false;
    int16_t ac_power;
    uint16_t soc;
    char mqtt_message[100];

    if ((argc == 2) && (strcmp(argv[1], "charge") == 0)) {
        charge_only = true;
    }

    signal(SIGINT, handle_sigint);

    ctx = modbus_new_tcp("10.0.0.200", 5020);
//    ctx = modbus_new_tcp("10.0.0.212", 502);
    if (ctx == NULL) {
        fprintf(stderr, "Kontext-Erstellung fehlgeschlagen\n");
        return -1;
    }
    modbus_set_response_timeout(ctx, 2, 0);

    mosquitto_lib_init();
    mosq = mosquitto_new("storage-client", true, NULL);
    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);

    while (mosquitto_connect(mosq, "127.0.0.1", 1883, 60) != MOSQ_ERR_SUCCESS) {
        sleep(30);
    }
    mosq_fd = mosquitto_socket(mosq);
    mosquitto_subscribe(mosq, NULL, "home/smartmeter/actual", 0);
    mosq_connected = true;

    FD_ZERO(&rfds);
    while (keep_running) {
        if (!mosq_connected) {
            modbus_write_register(ctx, 42010, 0); // set force mode 'None'
            while (mosquitto_reconnect(mosq) != MOSQ_ERR_SUCCESS) {
                sleep(30);
            }
            mosq_fd = mosquitto_socket(mosq);
            /* subscribe to all configured topics extended by 'set' */
            mosquitto_subscribe(mosq, NULL, "home/smartmeter/actual", 0);
        }
        if (!modbus_connected) {
            printf("modbus reconnect\n");
            sleep(10);
            if (modbus_connect(ctx) == 0) {
                if ((modbus_write_register(ctx, 42000, 0x55aa) == 1) && // enable RS485 control mode
                    (modbus_write_register(ctx, 42010, 0) == 1)) { // set force mode 'None'
                    modbus_connected = true;
                    char ch;
                    while (recv(mosq_fd, &ch, sizeof(ch), MSG_DONTWAIT) > 0);
                }
            }
        }

        FD_SET(mosq_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        ret = select(mosq_fd + 1, &rfds, 0, 0, &tv);
        if ((ret > 0) && FD_ISSET(mosq_fd, &rfds)) {
            mosquitto_loop_read(mosq, 1);
        }
        if (mosquitto_want_write(mosq)) {
            mosquitto_loop_write(mosq, 1);
        }
        mosquitto_loop_misc(mosq);

        if (!modbus_connected) {
            continue;
        }

        if (new_meter) {
#if 0
            modbus_read_registers(ctx, 37005, 1, &soc);
            modbus_read_registers(ctx, 30006, 1, (uint16_t *)&ac_power);
            printf("        grid %dW, AC power %dW, energy %d%%\n", grid_power, ac_power, soc);
#endif
            new_meter = false;
            if (delay) {
                delay--;
                continue;
            }
        } else {
            continue;
        }

        delay = 5;

        // grid_power < 0: Netzeinspeisung
        // grid_power > 0: Netzbezug
        // storage_power < 0: Speicher laden
        // storage_power > 0: Speicher entladen

        if (modbus_read_registers(ctx, 30006, 1, (uint16_t *)&ac_power) != 1) {
            modbus_connected = false;
            modbus_close(ctx);
            continue;
        }
        if (modbus_read_registers(ctx, 37005, 1, &soc) != 1) {
            modbus_connected = false;
            modbus_close(ctx);
            continue;
        }
        storage_power = (int)ac_power;

        if ((storage_power + grid_power) < -120) {
            storage_power += grid_power + 0; // auf 0W Einspeisung regeln
            if (soc == 100) {
               limit = 0;
            } else if (soc >= 95) {
               limit = -250;
            } else if (soc >= 90) {
               limit = -500;
            } else {
               limit = -2500;
            }
//limit = -2000;
            if (storage_power < limit) {
                storage_power = limit;
            }

            force_mode = 1; // charge
            force_charge_power = abs(storage_power);
            force_discharge_power = 0;
            printf("grid %dW storage %dW charge %dW\n", grid_power, ac_power, abs(storage_power));
        } else if (!charge_only && ((storage_power + grid_power) > 120)) {
            storage_power += grid_power - 0; // auf 0W Netzbezug regeln
            if (soc <= 12) {
               limit = 0;
            } else {
               limit = 2000;
            }
//limit=800;
            if (storage_power > limit) {
                storage_power = limit;
            }

            force_mode = 2; // discharge
            force_charge_power = 0;
            force_discharge_power = storage_power;
            printf("grid %dW storage %dW discharge %dW\n", grid_power, ac_power, storage_power);
        } else {
            force_mode = 0;
            force_charge_power = 0;
            force_discharge_power = 0;
            storage_power = 0;
            printf("grid %dW off\n", grid_power);
        }

        if ((modbus_write_register(ctx, 42020, force_charge_power) != 1)    ||
            (modbus_write_register(ctx, 42021, force_discharge_power) != 1) ||
            (modbus_write_register(ctx, 42010, force_mode) != 1)) {
            modbus_connected = false;
            modbus_close(ctx);
        }

        // publish mqtt
        ret = snprintf(mqtt_message, sizeof(mqtt_message), "{\"ac-power\":%d,\"grid-power\":%d,\"soc\":%d}",
                       ac_power, grid_power, soc);
        if ((ret > 0) && (ret < (int)sizeof(mqtt_message))) {
            mosquitto_publish(mosq, 0, "home/storage/actual", len, mqtt_message, 1, false);
        }

        fflush(stdout);
    }

    modbus_write_register(ctx, 42010, 0); // set force mode 'None'
    modbus_write_register(ctx, 42020, 0); // set charge power 0
    modbus_write_register(ctx, 42021, 0); // set discharge power 0
    modbus_write_register(ctx, 42000, 0x55bb); // disable RS485 control mode

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}
