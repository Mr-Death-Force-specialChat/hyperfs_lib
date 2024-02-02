OBJ=obj
TARGET=libhyperfs.so
FLAGS_C=
FLAGS_L=-fPIC -shared

CPP_SOURCES=$(wildcard *.cpp)
H_SOURCES=$(wildcard *.h)
OBJECTS=$(patsubst %.cpp,$(OBJ)/%.o,$(CPP_SOURCES))

build: $(TARGET)

$(TARGET): $(OBJECTS)
	g++ $(OBJECTS) -o $(TARGET) $(FLAGS_L)

$(OBJ)/%.o: %.cpp
	g++ -c $^ -o $@ $(FLAGS_C)

setup:
	mkdir $(OBJ)

clean:
	rm -r $(OBJ)
	mkdir $(OBJ)
