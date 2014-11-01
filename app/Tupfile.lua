DoOnce 'library/Tupfile.lua'

LuxemogApp = Define.Executable
{
	Name = 'luxemog',
	Sources = Item() + 'main.cxx',
	LocalLibraries = Luxemog,
	LinkFlags = ' -lluxem-cxx'
}

