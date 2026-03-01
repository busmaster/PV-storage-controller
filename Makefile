

CC = gcc
CFLAGS = -Wall -g
TARGET = pv-control
LIBS = -lmodbus -lcjson -lmosquitto
all: $(TARGET)

$(TARGET): pv-control.cpp
	$(CC) $(CFLAGS) -o $(TARGET) pv-control.cpp $(LIBS)

# Aufräumen: Erstellte Dateien löschen
clean:
	rm -f $(TARGET)
