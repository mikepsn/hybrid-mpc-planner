
#include <components.hxx>
#include <utils/loader.hxx>
#include <utils/component_factory.hxx>
#include <fstrips/loader.hxx>

fs0::Problem* generate(const rapidjson::Document& data, const std::string& data_dir) {
	fs0::BaseComponentFactory factory;
	fs0::fstrips::LanguageJsonLoader::loadLanguageInfo(data);
	fs0::Loader::loadProblemInfo(data, data_dir, factory);
	return fs0::Loader::loadProblem(data);
}
