DoOnce 'library/Tupfile.lua'

for Index, Source in ipairs(tup.glob '*.cxx')
do
	Executable = Define.Executable
	{
		Name = tup.base(Source),
		Sources = Item(Source),
		Objects = Luxemog,
		LinkFlags = ' -lluxem-cxx'
	}

	Define.Test
	{
		Executable = Executable
	}
end

