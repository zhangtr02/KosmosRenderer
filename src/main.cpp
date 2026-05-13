#include "App/Application.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

namespace
{
kosmos::app::ApplicationConfig ParseApplicationConfig(int argc, char** argv)
{
    kosmos::app::ApplicationConfig config;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--frames" && i + 1 < argc)
        {
            config.maxFrames = static_cast<unsigned int>(std::strtoul(argv[++i], nullptr, 10));
        }
        else if (argument == "--width" && i + 1 < argc)
        {
            config.width = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (argument == "--height" && i + 1 < argc)
        {
            config.height = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
    }

    return config;
}
}

int main(int argc, char** argv)
{
    try
    {
        kosmos::app::Application application(ParseApplicationConfig(argc, argv));
        return application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "KosmosRenderer fatal error: " << exception.what() << '\n';
        return 1;
    }
}
