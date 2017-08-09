# This SConstruct file is automatically moved to the directory where the generated
# instance-specific problem data resides during the process of generating the executable solver.

import os

HOME = os.path.expanduser("~")

# read variables from the cache, a user's custom.py file or command line arguments
vars = Variables(['variables.cache', 'custom.py'], ARGUMENTS)
vars.Add(BoolVariable('debug', 'Whether this is a debug build', 'no'))
vars.Add(BoolVariable('edebug', 'Extreme debug', 'no'))
vars.Add(PathVariable('lapkt', 'Path where the LAPKT library is installed', os.getenv('LAPKT2_PATH', ''), PathVariable.PathIsDir))
vars.Add(PathVariable('fs', 'Path where the FS+ library is installed', os.getenv('FS_PATH', ''), PathVariable.PathIsDir))

def which(program):
	""" Helper function emulating unix 'which' command """
	for path in os.environ["PATH"].split(os.pathsep):
		path = path.strip('"')
		exe_file = os.path.join(path, program)
		if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
			return exe_file
	return None

# Read the preferred compiler from the environment - if none specified, choose CLANG if possible
default_compiler = 'clang++' if which("clang++") else 'g++'
gcc = os.environ.get('CXX', default_compiler)

env = Environment(variables=vars, ENV=os.environ, CXX=gcc)

lapkt2_header_dir = os.path.join(env['lapkt'], 'include')
lapkt2_lib_dir = os.path.join(env['lapkt'], 'lib')

env.Append( CCFLAGS = ['-Wall', '-pedantic', '-std=c++14' ] )  # Flags common to all options
env.Append( LINKFLAGS = ['-lstdc++' ] )  # Seems to be necessary to avoid compatibility issues at linking time...

if env['edebug']:
	env.Append( CCFLAGS = ['-g', '-DDEBUG', '-DEDEBUG' ] )
	fs_libname = 'fs-edebug'
	lapkt_lib_sufix = '-edebug'
	lib_name = 'fs_edebug.so'
elif env['debug']:
	env.Append( CCFLAGS = ['-g', '-DDEBUG' ] )
	fs_libname = 'fs-debug'
	lapkt_lib_sufix = '-debug'
	lib_name = 'fs_debug.so'
else:
	env.Append( CCFLAGS = ['-O3', '-DNDEBUG' ] )
	fs_libname = 'fs'
	lapkt_lib_sufix = ''
	lib_name = 'fs.so'

# Header and library directories.
# We include pre-specified '~/local/include' and '~/local/lib' directories in case local versions of some libraries (e.g. Boost) are needed
include_paths = ['.', env['fs'] + '/src', lapkt2_header_dir]
isystem_paths = [HOME + '/local/include']
lib_paths = [env['fs'] + '/lib', lapkt2_lib_dir, HOME + '/local/lib']

# Boost Python settings
include_paths.append( '/usr/include/python2.7' )

env.Append( CCFLAGS = '-fPIC' )
env.Append( LIBPATH = [ '/usr/lib/python2.7/config' ] )
env.Append( LIBS = [ '-lboost_python', '-lpython2.7' ] )
env['STATIC_AND_SHARED_OBJECTS_ARE_THE_SAME']=1

env.Append(CPPPATH = [ os.path.abspath(p) for p in include_paths ])
env.Append(CCFLAGS = ['-isystem' + os.path.abspath(p) for p in isystem_paths])

src_objs = []
# FS main library:
env.Append(LIBS=[fs_libname])

# LAPKT lib dependencies:
env.Append(LIBS=[
	'liblapkt-novelty' + lapkt_lib_sufix + '.so',
	'liblapkt' + lapkt_lib_sufix + '.so',
])

# Other dependencies:
env.Append(LIBS=['boost_program_options', 'boost_serialization', 'boost_system', 'boost_timer', 'boost_chrono', 'rt', 'boost_filesystem', 'm'])


env.Append(LIBPATH=[ os.path.abspath(p) for p in lib_paths ])

Export('env')
Export('src_objs')
SConscript( 'gecode_dependencies' )
SConscript( 'soplex_dependencies' )
SConscript( 'ompl_dependencies' )
SConscript( 'src/SConscript')

env.SharedLibrary(lib_name, src_objs )
