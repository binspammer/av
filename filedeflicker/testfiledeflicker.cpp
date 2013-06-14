#include "filedeflicker.h"

#include <iostream>
#include <exception>
#include <stdexcept>

void testDeflicker(std::string src, std::string dst)
{
  FileDeflicker fileDeflicker(src, dst);
  fileDeflicker.init();
  fileDeflicker.process();
}

int
main (int argc, char *argv[])
try
{
  if (argc != 3) {
    std::cerr <<"usage: "<<argv[0] <<" input_file video_output_file \n"
              <<"API example program to show how to read frames from an input file.\n"
              <<"This program reads frames from a file, decodes, deflickes, encode them, \n"
              <<"and writes to a file named video_output_file.\n";
    exit(1);
  }
  //
  testDeflicker(argv[1], argv[2]);

}
catch(exception& e)
{
  std::cout<<e.what()<<std::endl;
}
