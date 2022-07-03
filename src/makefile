
portaudio_includes = include

C_INCLUDES = -I$/$(portaudio_includes)  

#CCFLAGS= $(C_INCLUDES) -Ofast -mfpu=vfp -mfloat-abi=hard -march=armv6zk -mtune=arm1176jzf-s
CCFLAGS= $(C_INCLUDES) -Ofast -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8

LIBS = -lpthread -lArduiPi_OLED -levdev 

prefix := /usr/local

# define all programs
PROGRAMS = sampleplayer 
SOURCES = ${PROGRAMS:=.cpp}

CXX=g++
CFLAGS=${CCFLAGS}

all: ${PROGRAMS}

${PROGRAMS}: ${SOURCES}
	$(CXX) $(CFLAGS) -Wall  $@.cpp $(LIBS) libportaudio.a libasound.so bcm2835.o  -o $@  

clean:
	rm -rf $(PROGRAMS)


