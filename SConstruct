# This SConstruct file is automatically moved to the directory where the generated
# instance-specific problem data resides during the process of generating the executable solver.

import os

HOME = os.path.expanduser("~")

# read variables from the cache, a user's custom.py file or command line arguments
vars = Variables(['variables.cache', 'custom.py'], ARGUMENTS)
vars.Add(BoolVariable('debug', 'Whether this is a debug build', 'no'))
vars.Add(BoolVariable('edebug', 'Extreme debug', 'no'))
vars.Add(PathVariable('lapkt', 'Path where the LAPKT library is installed', os.getenv('FS_LAPKT_PATH', ''), PathVariable.PathIsDir))
vars.Add(PathVariable('fs0', 'Path where the FS+ library is installed', os.getenv('FS_PLUS_PATH', ''), PathVariable.PathIsDir))

def which(program):
	""" Helper function emulating unix 'which' command """
	for path in os.environ["PATH"].split(os.pathsep):
		path = path.strip('"')
		exe_file = os.path.join(path, program)
		if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
			return exe_file
	return None

env = Environment(variables=vars, ENV=os.environ, CXX='clang' if which('clang') else 'g++')


env.Append( CCFLAGS = ['-Wall', '-pedantic', '-std=c++11' ] )  # Flags common to all options
env.Append( LINKFLAGS = ['-lstdc++' ] )  # Seems to be necessary to avoid compatibility issues at linking time...

if env['edebug']:
	env.Append( CCFLAGS = ['-g', '-DDEBUG', '-DFS0_DEBUG' ] )
	fs0_libname = 'fs0-edebug'
	lib_name = 'fs_edebug.so'
elif env['debug']:
	env.Append( CCFLAGS = ['-g', '-DDEBUG' ] )
	fs0_libname = 'fs0-debug'
	lib_name = 'fs_debug.so'
else:
	env.Append( CCFLAGS = ['-O3', '-DNDEBUG' ] )
	fs0_libname = 'fs0'
	lib_name = 'fs.so'

# Header and library directories.
# We include pre-specified '~/local/include' and '~/local/lib' directories in case local versions of some libraries (e.g. Boost) are needed
include_paths = ['.', env['fs0'] + '/src', env['lapkt'], env['lapkt'] + '/interfaces/agnostic', HOME + '/local/include']
lib_paths = [env['fs0'] + '/lib', HOME + '/local/lib']

# Boost Python settings
include_paths.append( '/usr/include/python2.7' )

env.Append( CCFLAGS = '-fPIC' )
env.Append( LIBPATH = [ '/usr/lib/python2.7/config' ] )
env.Append( LIBS = [ '-lboost_python', '-lpython2.7' ] )
env['STATIC_AND_SHARED_OBJECTS_ARE_THE_SAME']=1

env.Append(CPPPATH = [ os.path.abspath(p) for p in include_paths ])

src_objs = []
env.Append(LIBS=[fs0_libname, 'boost_program_options', 'boost_serialization', 'boost_system', 'boost_timer', 'boost_chrono', 'rt', 'boost_filesystem', 'm'])
env.Append(LIBPATH=[ os.path.abspath(p) for p in lib_paths ])

Export('env')
Export('src_objs')
SConscript( 'gecode_dependencies' )
SConscript( 'ompl_dependencies' )
SConscript( 'src/SConscript')

env.SharedLibrary(lib_name, src_objs )
