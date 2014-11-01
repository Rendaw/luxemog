#ifndef luxemog_h
#define luxemog_h

#include <luxem/luxem.h>

namespace luxemog
{

struct transformation
{
	transformation(std::shared_ptr<luxem::reader::object_context> &&object);
	void apply(std::shared_ptr<luxem::value> &target);

	private:
		std::shared_ptr<luxem::value> from, to;
};

}

#endif

