# Host tests: the controller logic (everything except USB, sensor and model)
# compiled for your PC.   make -C firmware/swing_to_mine/test
CC ?= cc
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -I../src -fsanitize=address,undefined
LOGIC = ../src/controller.c ../src/button.c ../src/motion.c ../src/air_mouse.c \
        ../src/tilt_walk.c ../src/gesture_decoder.c

all: test_controller replay
	./test_controller

test_controller: test_controller.c $(LOGIC) ../src/*.h
	$(CC) $(CFLAGS) -o $@ test_controller.c $(LOGIC) -lm

replay: replay.c $(LOGIC) ../src/*.h
	$(CC) $(CFLAGS) -o $@ replay.c $(LOGIC) -lm

libdecoder.so: ../src/gesture_decoder.c ../src/gesture_decoder.h
	$(CC) -std=c11 -O1 -shared -fPIC -I../src -o $@ ../src/gesture_decoder.c

parity: libdecoder.so
	python3 test_decoder_parity.py

clean:
	rm -f test_controller replay libdecoder.so

.PHONY: all clean parity
