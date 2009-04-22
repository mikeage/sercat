CPP := mingw32-g++

CPPFLAGS := -Wall -g -O2

SERCAT_SRC = sercat.cpp
SERCAT_EXE = sercat.exe
SERCAT_ZIP = sercat.zip
SERCAT_OBJ = $(SERCAT_SRC:.cpp=.o)

$(SERCAT_EXE): $(SERCAT_OBJ)
	$(CPP) $(LDFLAGS) $(SERCAT_OBJ) -o $@

%.o: %.cpp
	$(CPP) -c $(CPPFLAGS) $*.cpp

clean:
	$(RM) $(SERCAT_OBJ) $(SERCAT_EXE)

zip: $(SERCAT_EXE)
	zip $(SERCAT_ZIP) $(SERCAT_EXE) $(SERCAT_SRC) gpl.txt Makefile
