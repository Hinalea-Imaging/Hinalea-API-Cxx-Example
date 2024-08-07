#include "MainWindow.hxx"

#include <Hinalea/Version.h>

#if !HINALEA_VERSION_CHECK( 2, 0, 0 )
#  error "This example application requires Hinalea API v2."
#endif

#include <QApplication>
#include <QStyleFactory>

#include <iostream>

#include <cstdlib>

namespace {

[[ nodiscard ]]
auto formatLog(
    HINALEA_IN ::hinalea::Log const logFlag
    ) noexcept -> ::std::string_view
{
    switch ( logFlag )
    {
        case ::hinalea::Log::Debug:    { return "Debug";    }
        case ::hinalea::Log::Info:     { return "Info";     }
        case ::hinalea::Log::Warning:  { return "Warning";  }
        case ::hinalea::Log::Error:    { return "Error";    }
        case ::hinalea::Log::Critical: { return "Critical"; }
        default: { HINALEA_UNREACHABLE( );  }
    }
}

auto logCallback(
    HINALEA_IN   ::hinalea::Log const logFlag,
    HINALEA_IN_Z char const * const   message,
    HINALEA_IN_Z char const * const   file_name,
    HINALEA_IN_Z char const * const   function_name,
    HINALEA_IN   ::hinalea::Int const line
    ) -> void
{
    ::std::cerr
        << ::formatLog( logFlag )
        << "\n | file: " << file_name
        << "\n | func: " << function_name
        << "\n | line: " << line
        << "\n | mesg: " << message
        << '\n';
}

auto setupApplication(
    ) -> void
{
    QApplication::setOrganizationName( "Hinalea" );
    QApplication::setOrganizationDomain( "hinaleaimaging.com" );
    QApplication::setApplicationName( "Hinalea API Example App" );

    auto * const style = QStyleFactory::create( "Fusion" );
    QApplication::setStyle( style );
}

} /* namespace anonymous */

auto main(
    HINALEA_IN int     argc,
    HINALEA_IN char ** argv
    ) -> int
try
{
    ::std::cout
        << "[ Hinalea API Version: " << ::hinalea::build_info::library_version_string( )
        << ", Qt Version: " << QT_VERSION_STR << " ]"
        << ::std::endl;

    ::hinalea::log::set_log_callback< &::logCallback >(
#if 0
        ::hinalea::Log::All
#else
        // ::hinalea::Log::Info |
        // ::hinalea::Log::Warning |
        ::hinalea::Log::Error |
        ::hinalea::Log::Critical
#endif
        );

    ::setupApplication( );

    auto application = QApplication{ argc, argv };
    auto mainWindow = MainWindow{ };
    mainWindow.show( );
    return application.exec( );
}
catch( ::std::exception const & exc )
{
    ::hinalea::log::critical( exc.what( ) );
    return EXIT_FAILURE;
}
