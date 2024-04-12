#include "MainWindow.hxx"

#include <Hinalea/Print.hxx>

#include <QApplication>
#include <QStyleFactory>

#include <iostream>

#include <cstdlib>

#line HINALEA_FILENAME( "Main.cxx" )

[[ nodiscard ]]
auto fmt_log(
    HINALEA_IN ::hinalea::Log const log_flag
    ) noexcept -> ::std::string_view
{
    switch ( log_flag )
    {
        case ::hinalea::Log::Debug:    { return "Debug";    }
        case ::hinalea::Log::Info:     { return "Info";     }
        case ::hinalea::Log::Warning:  { return "Warning";  }
        case ::hinalea::Log::Error:    { return "Error";    }
        case ::hinalea::Log::Critical: { return "Critical"; }
        default: { HINALEA_UNREACHABLE( );  }
    }
}

auto log_callback(
    HINALEA_IN   ::hinalea::Log const log_flag,
    HINALEA_IN_Z char const * const   message,
    HINALEA_IN_Z char const * const   file_name,
    HINALEA_IN_Z char const * const   function_name,
    HINALEA_IN   ::hinalea::Int const line
    ) -> void
{
    ::std::cerr
        << fmt_log( log_flag )
        << "\n | file: " << file_name
        << "\n | func: " << function_name
        << "\n | line: " << line
        << "\n | mesg: " << message
        << '\n';
}

auto setupApplication(
    ) -> void
{
    QCoreApplication::setOrganizationName( "Hinalea" );
    QCoreApplication::setOrganizationDomain( "hinalea.com" );
    QCoreApplication::setApplicationName( "Hinalea API Example App" );

    auto * const style = QStyleFactory::create( "Fusion" );
    QApplication::setStyle( style );
}

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

    ::hinalea::log::set_log_callback< &log_callback >(
        ::hinalea::Log::All
        // ::hinalea::Log::Warning | ::hinalea::Log::Error | ::hinalea::Log::Critical
        );

    ::setupApplication( );

    auto application = QApplication{ argc, argv };
    auto mainWindow = MainWindow{ };
    mainWindow.show( );
    return application.exec( );
}
catch( ::std::exception const & exc )
{
    ::hinalea::log::critical( exc.what( ), __FILE__, __func__, __LINE__ );
    return EXIT_FAILURE;
}
