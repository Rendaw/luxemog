#ifndef luxemog_h
#define luxemog_h

#include <luxem-cxx/luxem.h>

namespace luxemog
{

struct transform
{
	transform(std::shared_ptr<luxem::value> &&data);
	void apply(std::shared_ptr<luxem::value> &target);

	private:
		std::shared_ptr<luxem::value> from, to;
};

struct transform_list
{
	transform_list(void);
	void deserialize(luxem::reader::array_context &context);

	void apply(std::shared_ptr<luxem::value> &target);

	private:
		std::list<std::unique_ptr<transform>> transforms;
};

}

#endif

