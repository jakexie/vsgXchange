/* <editor-fold desc="MIT License">

Copyright(c) 2019 Thomas Hogarth and Robert Osfield

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgXchange/models.h>

#include <osg/TransferFunction>
#include <osg/AnimationPath>
#include <osg/io_utils>
#include <osgDB/PluginQuery>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgUtil/MeshOptimizers>
#include <osgUtil/Optimizer>

#include "ConvertToVsg.h"
#include "ImageUtils.h"
#include "Optimize.h"
#include "SceneBuilder.h"

#include <iostream>

using namespace vsgXchange;

namespace osg2vsg
{
    struct PipelineCache;
}

namespace vsgXchange
{

    class OSG::Implementation
    {
    public:
        Implementation();

        vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options = {}) const;

        bool readOptions(vsg::Options& options, vsg::CommandLine& arguments) const;

        vsg::ref_ptr<osg2vsg::PipelineCache> pipelineCache;

    protected:
    };

} // namespace vsgXchange

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// OSG ReaderWriter fascade
//
OSG::OSG() :
    _implementation(new OSG::Implementation())
{
}
OSG::~OSG()
{
    delete _implementation;
};
bool OSG::readOptions(vsg::Options& options, vsg::CommandLine& arguments) const
{
    return _implementation->readOptions(options, arguments);
}

vsg::ref_ptr<vsg::Object> OSG::read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) const
{
    return _implementation->read(filename, options);
}

bool OSG::getFeatures(Features& features) const
{
    osgDB::FileNameList all_plugins = osgDB::listAllAvailablePlugins();
    osgDB::FileNameList plugins;
    for (auto& filename : all_plugins)
    {
        // the plugin list icludes the OSG's serializers so we need to discard these from being queried.
        if (filename.find("osgdb_serializers_") == std::string::npos && filename.find("osgdb_deprecated_") == std::string::npos)
        {
            plugins.push_back(filename);
        }
    }

    osgDB::ReaderWriterInfoList infoList;
    for (auto& pluginName : plugins)
    {
        osgDB::queryPlugin(pluginName, infoList);
    }

    for (auto& info : infoList)
    {
        for (auto& ext_description : info->extensions)
        {
            features.extensionFeatureMap[ext_description.first] = vsg::ReaderWriter::READ_FILENAME;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// OSG ReaderWriter implementation
//
OSG::Implementation::Implementation()
{
    pipelineCache = osg2vsg::PipelineCache::create();
}

bool OSG::Implementation::readOptions(vsg::Options& options, vsg::CommandLine& arguments) const
{
    if (arguments.read("--original")) options.setValue("original", true);

    std::string filename;
    if (arguments.read("--read-osg", filename))
    {
        auto defaults = vsg::read(filename);
        std::cout << "vsg::read(" << filename << ") defaults " << defaults << std::endl;
        if (defaults)
        {
            options.setObject("osg", defaults);
        }
    }

    if (arguments.read("--write-osg", filename))
    {
        auto defaults = osg2vsg::BuildOptions::create();
        if (vsg::write(defaults, filename))
        {
            std::cout << "Written osg2vsg defaults to: " << filename << std::endl;
        }
    }
    return false;
}

vsg::ref_ptr<vsg::Object> OSG::Implementation::read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) const
{
    osg::ref_ptr<osgDB::Options> osg_options;

    if (options)
    {
        if (osgDB::Registry::instance()->getOptions())
            osg_options = osgDB::Registry::instance()->getOptions()->cloneOptions();
        else
            osg_options = new osgDB::Options();
        osg_options->getDatabasePathList().insert(osg_options->getDatabasePathList().end(), options->paths.begin(), options->paths.end());
    }

    auto ext = vsg::lowerCaseFileExtension(filename);
    if (ext=="path")
    {
        auto foundPath = vsg::findFile(filename, options);
        if (!foundPath.empty())
        {
            std::ifstream fin(foundPath);
            if (!fin) return {};

            osg::ref_ptr<osg::AnimationPath> osg_animationPath = new osg::AnimationPath;
            osg_animationPath->read(fin);

            auto vsg_animationPath = vsg::AnimationPath::create();

            switch(osg_animationPath->getLoopMode())
            {
                case(osg::AnimationPath::SWING):
                    vsg_animationPath->mode = vsg::AnimationPath::FORWARD_AND_BACK;
                    break;
                case(osg::AnimationPath::LOOP):
                    vsg_animationPath->mode = vsg::AnimationPath::REPEAT;
                    break;
                case(osg::AnimationPath::NO_LOOPING):
                    vsg_animationPath->mode = vsg::AnimationPath::ONCE;
                    break;
            }

            for(auto& [time, cp] : osg_animationPath->getTimeControlPointMap())
            {
                const auto& position = cp.getPosition();
                const auto& rotation = cp.getRotation();
                // const auto& scale = cp.getScale(); // TODO

                vsg_animationPath->add(time, vsg::dvec3(position.x(), position.y(), position.z()), vsg::dquat(rotation.x(), rotation.y(), rotation.z(), rotation.w()));
            }

            return vsg_animationPath;
        }
        return {};
    }

    osg::ref_ptr<osg::Object> object = osgDB::readRefObjectFile(filename, osg_options.get());
    if (!object)
    {
        return {};
    }

    bool mapRGBtoRGBAHint = !options || options->mapRGBtoRGBAHint;

    if (osg::Node* osg_scene = object->asNode(); osg_scene != nullptr)
    {
        vsg::Paths searchPaths = vsg::getEnvPaths("VSG_FILE_PATH"); // TODO, use the vsg::Options ?

        // check to see if osg_options have been assigned to vsg::Options
        auto default_options = options->getObject<osg2vsg::BuildOptions>("osg");

        // clone the osg specific buildOptions if they have been assign to vsg::Options
        vsg::ref_ptr<osg2vsg::BuildOptions> buildOptions;
        if (default_options)
        {
            buildOptions = osg2vsg::BuildOptions::create(*default_options);
        }
        else
        {
            buildOptions = osg2vsg::BuildOptions::create();
            buildOptions->mapRGBtoRGBAHint = mapRGBtoRGBAHint;
        }

        buildOptions->options = options;
        buildOptions->pipelineCache = pipelineCache;

        bool original_conversion = false;
        options->getValue("original", original_conversion);
        if (original_conversion)
        {
            osg2vsg::SceneBuilder sceneBuilder(buildOptions);
            auto vsg_scene = sceneBuilder.optimizeAndConvertToVsg(osg_scene, searchPaths);
            return vsg_scene;
        }
        else
        {
            vsg::ref_ptr<vsg::StateGroup> inheritedStateGroup;

            osg2vsg::ConvertToVsg sceneBuilder(buildOptions, inheritedStateGroup);

            sceneBuilder.optimize(osg_scene);
            auto vsg_scene = sceneBuilder.convert(osg_scene);

            if (sceneBuilder.numOfPagedLOD > 0)
            {
                uint32_t maxLevel = 20;
                uint32_t estimatedNumOfTilesBelow = 0;
                uint32_t maxNumTilesBelow = 40000;

                uint32_t level = 0;
                for (uint32_t i = level; i < maxLevel; ++i)
                {
                    estimatedNumOfTilesBelow += std::pow(4, i - level);
                }

                uint32_t tileMultiplier = std::min(estimatedNumOfTilesBelow, maxNumTilesBelow) + 1;

                vsg::CollectResourceRequirements collectResourceRequirements;
                vsg_scene->accept(collectResourceRequirements);
                vsg_scene->setObject("ResourceHints", collectResourceRequirements.createResourceHints(tileMultiplier));
            }
            return vsg_scene;
        }
    }
    else if (osg::Image* osg_image = dynamic_cast<osg::Image*>(object.get()); osg_image != nullptr)
    {
        return osg2vsg::convertToVsg(osg_image, mapRGBtoRGBAHint);
    }
    else if (osg::TransferFunction1D* tf = dynamic_cast<osg::TransferFunction1D*>(object.get()); tf != nullptr)
    {
        auto tf_image = tf->getImage();
        auto vsg_image = osg2vsg::convertToVsg(tf_image, mapRGBtoRGBAHint);
        vsg_image->setValue("Minimum", tf->getMinimum());
        vsg_image->setValue("Maximum", tf->getMaximum());

        return vsg_image;
    }
    else
    {
        std::cout << "OSG::ImplementationreadFile(" << filename << ") cannot convert object type " << object->className() << "." << std::endl;
    }

    return {};
}
