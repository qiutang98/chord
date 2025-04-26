#include "flower.h"

int main(int argc, const char** argv)
{
	int exitCode = Flower::get().run(argc, argv);
	std::exit(exitCode);
}