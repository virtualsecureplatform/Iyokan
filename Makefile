CXXFLAGS=-std=c++17 -Wall -Wextra -pedantic
CXXFLAGS_DEBUG=$(CXXFLAGS) -g3 -O0 -fPIC
CXXFLAGS_SANITIZE=$(CXXFLAGS) -O0 -g3 \
				  -fsanitize=address,undefined -fno-omit-frame-pointer \
				  -fno-optimize-sibling-calls
CXXFLAGS_RELEASE=$(CXXFLAGS) -O3 -march=native -g3
INC=-I picojson/
LIB=-lpthread

main: main.cpp main.hpp plain.hpp
	clang++ $(CXXFLAGS_SANITIZE) -o $@ $< $(INC) $(LIB)
	#clang++ $(CXXFLAGS_DEBUG) -o $@ $< $(INC) $(LIB)
	#clang++ $(CXXFLAGS_RELEASE) -o $@ $< $(INC) $(LIB)
