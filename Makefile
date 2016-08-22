BIN=arc++
CXXFLAGS=-Wall -O3 -c -std=c++11
LDFLAGS=-s -lm

$(BIN): main.o arc.o
	$(CXX) -o $(BIN) main.o arc.o $(LDFLAGS)

readline: CXXFLAGS+=-DREADLINE
readline: LDFLAGS+=-lreadline
readline: $(BIN)

mingw: CXXFLAGS=-Wall -O3 -c -std=gnu++11
mingw: main.o arc.o ico.o
	$(CXX) -o $(BIN) main.o arc.o ico.o $(LDFLAGS)

ico.o: arc.rc arc.ico
	windres -o ico.o -O coff arc.rc

main.o: main.cpp arc.h
	$(CXX) $(CXXFLAGS) main.cpp
arc.o: arc.cpp arc.h library.h
	$(CXX) $(CXXFLAGS) arc.cpp
run: $(BIN)
	./$(BIN)
clean:
	rm -f $(BIN) *.o
tag:
	etags *.h *.cpp
