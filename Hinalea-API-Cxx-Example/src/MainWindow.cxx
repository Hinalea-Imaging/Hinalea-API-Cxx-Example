#include "MainWindow.hxx"
#include "ui_MainWindow.h"

#include <Hinalea/Print.hxx>

#include <QChart>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include <QImage>
#include <QLineSeries>
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSemaphoreReleaser>
#include <QSettings>
#include <QTimer>

#include <chrono>
#include <type_traits>

namespace {

[[ nodiscard ]]
auto cameraTypes(
    ) -> QMap< QString, ::hinalea::CameraType > const &
{
    static auto const map = QMap< QString, ::hinalea::CameraType >{
        { "Allied Vision Goldeye G-130"   , ::hinalea::CameraType::M_G_130_TEC1      },
        { "MatrixVision BlueFox3"         , ::hinalea::CameraType::M_BlueFox3_M2024C },
        { "Photometrics Kinetix"          , ::hinalea::CameraType::M_Kinetix         },
        { "Photometrics Prime BSI Express", ::hinalea::CameraType::M_PrimeBsiExpress },
        { "Raptor OWL 1280"               , ::hinalea::CameraType::M_Owl1280         },
        { "Raptor OWL 640M"               , ::hinalea::CameraType::M_Owl640M         },
        { "Ximea xiC MC023CG-SY-UB"       , ::hinalea::CameraType::M_MC023CG_SY_UB   },
        { "Ximea xiC MC050CG-SY-UB"       , ::hinalea::CameraType::M_MC050CG_SY_UB   },
        { "Ximea xiQ MQ003MG-CM"          , ::hinalea::CameraType::M_MQ003MG_CM      },
        };
    return map;
}

[[ nodiscard, maybe_unused ]]
auto pathCast(
    HINALEA_IN ::std::string const & path
    ) -> QString
{
    return QString::fromStdString( path );
}

[[ nodiscard, maybe_unused ]]
auto pathCast(
    HINALEA_IN ::std::wstring const & path
    ) -> QString
{
    return QString::fromStdWString( path );
}

[[ nodiscard ]]
auto pathCast(
    HINALEA_IN ::hinalea::fs::path const & path
    ) -> QString
{
    return ::pathCast( path.native( ) );
}

[[ nodiscard ]]
auto pathCast(
    HINALEA_IN QString const & path
    ) -> ::hinalea::fs::path
{
    if constexpr ( ::std::is_same_v< ::hinalea::fs::path::string_type, ::std::string > )
    {
        return path.toStdString( );
    }
    else
    {
        return path.toStdWString( );
    }
}

[[ nodiscard ]]
auto pathCast(
    HINALEA_IN QLineEdit const * const lineEdit
    ) -> ::hinalea::fs::path
{
    return ::pathCast( lineEdit->text( ) );
}

[[ nodiscard ]]
constexpr
auto exposureCast(
    HINALEA_IN int const value
    ) -> ::hinalea::MicrosecondsI
{
    if constexpr ( ::ui_exposure_is_milliseconds )
    {
        auto const msec = ::hinalea::MillisecondsI{ value };
        return ::hinalea::MicrosecondsI{ msec };
    }
    else
    {
        return ::hinalea::MicrosecondsI{ value };
    }
}

[[ nodiscard ]]
constexpr
auto gainCast(
    HINALEA_IN int const value
    ) -> ::hinalea::Real
{
    return static_cast< ::hinalea::Real >( value );
}

[[ nodiscard ]]
constexpr
auto gapIndexCast(
    HINALEA_IN int const value
    ) -> ::hinalea::Size
{
    return static_cast< ::hinalea::Size >( value );
}

[[ nodiscard ]]
constexpr
auto reflectanceCast(
    HINALEA_IN double const value
    ) -> ::hinalea::Real
{
    return static_cast< ::hinalea::Real >( value / 100.0 );
}

[[ nodiscard ]]
auto ioDir(
    ) -> ::hinalea::fs::path const &
{
    static auto const dir = ::hinalea::fs::current_path( );
    return dir;
}

auto joinThread(
    HINALEA_INOUT ::std::thread & thread
    ) -> void
{
    if ( thread.joinable( ) )
    {
        thread.join( );
    }
}

} /* namespace anonymous */

MainWindow::MainWindow(
    HINALEA_IN_OPT QWidget * const parent
    )
    : QMainWindow{ parent }
    , ui{ new Ui::MainWindow{ } }
    , displayTimer{ new QTimer{ this } }
    , displayItem{ new QGraphicsPixmapItem{ } }
    , classifyItem{ new QGraphicsPixmapItem{ } }
    , chart{ new QChart{ } }
    , seriesL{ new QLineSeries{ } }
    , seriesR{ new QLineSeries{ } }
    , seriesG{ new QLineSeries{ } }
    , seriesB{ new QLineSeries{ } }
{
    Q_SET_OBJECT_NAME( displayTimer );
    Q_SET_OBJECT_NAME( chart );
    Q_SET_OBJECT_NAME( seriesL );
    Q_SET_OBJECT_NAME( seriesR );
    Q_SET_OBJECT_NAME( seriesG );
    Q_SET_OBJECT_NAME( seriesB );

    ui->setupUi( this );
    this->setWindowTitle( QObject::tr( "Hinalea API (v" HINALEA_VERSION_STRING ") Qt (v" QT_VERSION_STR ") C++ Example" ) );

    HINALEA_ASSERT( ui->cameraComboBox->count( ) == 0 );
    ui->cameraComboBox->addItems( ::cameraTypes( ).keys( ) );

    ui->exposureSpinBox->setSuffix(
        ::ui_exposure_is_milliseconds
            ? QObject::tr( " msec" )
            : QObject::tr( " usec" )
        );

    this->initConnections( );
    this->initChartView( );
    this->initImageView( );
    this->initSpectralMetric( );

    this->enablePowerWidgets( false );

    this->loadSettings( );

    /* NOTE:
     * If MatrixVision is loaded before AlliedVision, it will throw "VmbErrorNoTL: No transport layers are found.".
     * Seems ok if AlliedVision is loaded first and then can safely switch between the two.
     */
    this->updateCameraType( );
}

MainWindow::~MainWindow(
    )
{
    this->cancel( );

    for ( auto thread : {
        ::std::ref( this->recordThread ),
        ::std::ref( this->realtimeThread ),
        ::std::ref( this->displayThread ),
        ::std::ref( this->processThread ),
    } )
    {
        ::joinThread( thread.get( ) );
    }

    this->powerOff( );
    this->saveSettings( );
}

auto MainWindow::loadSettings(
    ) -> void
{
    auto const settings = QSettings{ };

    ui->exposureSpinBox->setValue( settings.value( "exposure", 1 ).toInt( ) );
    ui->gainSpinBox    ->setValue( settings.value( "gain"    , 0 ).toInt( ) );
    ui->gapIndexSpinBox->setValue( settings.value( "gapIndex", 0 ).toInt( ) );
    ui->smoothSpinBox  ->setValue( settings.value( "smooth"  , 5 ).toInt( ) );

    ui->reflectanceSpinBox->setValue( settings.value( "reflectance", 95.0 ).toDouble( ) );
    ui->thresholdSpinBox  ->setValue( settings.value( "threshold"  ,  0.2 ).toDouble( ) );

    ui->darkLineEdit    ->setText( settings.value( "dark"     ).toString( ) );
    ui->gapLineEdit     ->setText( settings.value( "gaps"     ).toString( ) );
    ui->matrixLineEdit  ->setText( settings.value( "matrix"   ).toString( ) );
    ui->settingsLineEdit->setText( settings.value( "settings" ).toString( ) );
    ui->whiteLineEdit   ->setText( settings.value( "white"    ).toString( ) );

    ui->binningComboBox        ->setCurrentIndex( settings.value( "binning"     ).toInt( ) );
    ui->bitDepthComboBox       ->setCurrentIndex( settings.value( "bit_depth"   ).toInt( ) );
    ui->measurementTypeComboBox->setCurrentIndex( settings.value( "measurement" ).toInt( ) );
    ui->modeComboBox           ->setCurrentIndex( settings.value( "mode"        ).toInt( ) );

    ui->cameraComboBox->setCurrentText( settings.value( "camera" ).toString( ) );

    ui->horizontalCheckBox ->setChecked( settings.value( "flip_horizontal" ).toBool( ) );
    ui->verticalCheckBox   ->setChecked( settings.value( "flip_vertical"   ).toBool( ) );
    ui->reflectanceCheckBox->setChecked( settings.value( "use_reflectance" ).toBool( ) );

    if ( auto const geometry = settings.value( "geometry" ).toByteArray( );
         geometry.isEmpty( ) )
    {
        this->showMaximized( );
    }
    else
    {
        this->restoreGeometry( geometry );
    }

    this->updateDark( );
    this->updateWhite( );
}

auto MainWindow::saveSettings(
    ) const -> void
{
    auto settings = QSettings{ };

    settings.setValue( "exposure", ui->exposureSpinBox->value( ) );
    settings.setValue( "gain"    , ui->gainSpinBox    ->value( ) );
    settings.setValue( "gapIndex", ui->gapIndexSpinBox->value( ) );
    settings.setValue( "smooth"  , ui->smoothSpinBox  ->value( ) );

    settings.setValue( "reflectance", ui->reflectanceSpinBox->value( ) );
    settings.setValue( "threshold"  , ui->thresholdSpinBox  ->value( ) );

    settings.setValue( "dark"    , ui->darkLineEdit    ->text( ) );
    settings.setValue( "gaps"    , ui->gapLineEdit     ->text( ) );
    settings.setValue( "matrix"  , ui->matrixLineEdit  ->text( ) );
    settings.setValue( "settings", ui->settingsLineEdit->text( ) );
    settings.setValue( "white"   , ui->whiteLineEdit   ->text( ) );

    settings.setValue( "binning"    , ui->binningComboBox        ->currentIndex( ) );
    settings.setValue( "bit_depth"  , ui->bitDepthComboBox       ->currentIndex( ) );
    settings.setValue( "measurement", ui->measurementTypeComboBox->currentIndex( ) );
    settings.setValue( "mode"       , ui->modeComboBox           ->currentIndex( ) );

    settings.setValue( "camera", ui->cameraComboBox->currentText( ) );

    settings.setValue( "flip_horizontal", ui->horizontalCheckBox ->isChecked( ) );
    settings.setValue( "flip_vertical"  , ui->verticalCheckBox   ->isChecked( ) );
    settings.setValue( "use_reflectance", ui->reflectanceCheckBox->isChecked( ) );

    settings.setValue( "geometry", this->saveGeometry( ) );
}

auto MainWindow::initConnections(
    ) -> void
{
    /* NOTE:
     * Qt::QueuedConnection is required for signals emitted from a non-GUI thread.
     */
    QObject::connect(
        this,
        &MainWindow::progressChanged,
        this,
        &MainWindow::onProgressChanged,
        Qt::QueuedConnection
        );

    QObject::connect(
        this,
        &MainWindow::threadFailed,
        this,
        &MainWindow::onThreadFailed,
        Qt::QueuedConnection
        );

    QObject::connect(
        this,
        &MainWindow::doUpdateImage,
        this,
        &MainWindow::onUpdateImage,
        Qt::QueuedConnection
        );

    QObject::connect(
        this,
        &MainWindow::doUpdateSeries,
        this,
        &MainWindow::onUpdateSeries,
        Qt::QueuedConnection
        );

    QObject::connect(
        this,
        &MainWindow::doUpdateStatistics,
        this,
        &MainWindow::onUpdateStatistics,
        Qt::QueuedConnection
        );

    QObject::connect(
        this->displayTimer,
        &QTimer::timeout,
        this,
        &MainWindow::onDisplayTimerTimeout
        );

    QObject::connect(
        ui->powerButton,
        &QAbstractButton::toggled,
        this,
        &MainWindow::onPowerButtonToggled
        );

    QObject::connect(
        ui->recordButton,
        &QAbstractButton::toggled,
        this,
        &MainWindow::onRecordButtonToggled
        );

    QObject::connect(
        ui->reflectanceCheckBox,
        &QAbstractButton::toggled,
        this,
        &MainWindow::onReflectanceCheckBoxToggled
        );

    QObject::connect(
        ui->processButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onProcessButtonClicked
        );

    QObject::connect(
        ui->cameraComboBox,
        qOverload< int >( &QComboBox::currentIndexChanged ),
        this,
        &MainWindow::onCameraComboBoxCurrentIndexChanged
        );

    QObject::connect(
        ui->horizontalCheckBox,
        &QAbstractButton::toggled,
        this,
        &MainWindow::onHorizontalCheckBoxToggled
        );

    QObject::connect(
        ui->verticalCheckBox,
        &QAbstractButton::toggled,
        this,
        &MainWindow::onVerticalCheckBoxToggled
        );

    QObject::connect(
        ui->exposureSpinBox,
        qOverload< int >( &QSpinBox::valueChanged ),
        this,
        &MainWindow::onExposureSpinBoxValueChanged
        );

    QObject::connect(
        ui->gainSpinBox,
        qOverload< int >( &QSpinBox::valueChanged ),
        this,
        &MainWindow::onGainSpinBoxValueChanged
        );

    QObject::connect(
        ui->gainModeSpinBox,
        qOverload< int >( &QSpinBox::valueChanged ),
        this,
        &MainWindow::onGainModeSpinBoxValueChanged
        );

    QObject::connect(
        ui->gapIndexSpinBox,
        qOverload< int >( &QSpinBox::valueChanged ),
        this,
        &MainWindow::onGapIndexSpinBoxValueChanged
        );

    QObject::connect(
        ui->loadSettingsButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onLoadSettingsClicked
        );

    QObject::connect(
        ui->loadWhiteButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onLoadWhiteClicked
        );

    QObject::connect(
        ui->loadDarkButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onLoadDarkClicked
        );

    QObject::connect(
        ui->loadMatrixButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onLoadMatrixClicked
        );

    QObject::connect(
        ui->loadGapButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onLoadGapClicked
        );

    QObject::connect(
        ui->clearSettingsButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onClearSettingsClicked
        );

    QObject::connect(
        ui->clearWhiteButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onClearWhiteClicked
        );

    QObject::connect(
        ui->clearDarkButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onClearDarkClicked
        );

    QObject::connect(
        ui->clearMatrixButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onClearMatrixClicked
        );

    QObject::connect(
        ui->clearGapButton,
        &QAbstractButton::clicked,
        this,
        &MainWindow::onClearGapClicked
        );
}

auto MainWindow::initImageView(
    ) -> void
{
    ui->imageView->setScene( new QGraphicsScene{ this } );

    for ( auto * const item : ::std::initializer_list< QGraphicsItem * >{
        this->displayItem,
        this->classifyItem,
    } )
    {
        ui->imageView->scene( )->addItem( item );
    }
}

auto MainWindow::initChartView(
    ) -> void
{
    this->chart->addSeries( seriesL );
    this->chart->addSeries( seriesR );
    this->chart->addSeries( seriesG );
    this->chart->addSeries( seriesB );

    seriesL->setColor( Qt::gray  );
    seriesR->setColor( Qt::red   );
    seriesG->setColor( Qt::green );
    seriesB->setColor( Qt::blue  );

    this->chart->legend( )->hide( );
    this->chart->createDefaultAxes( );
    this->chart->setTitle( QObject::tr( "Spectra" ) );
    ui->chartView->setChart( chart );
}

auto MainWindow::initSpectralMetric(
    ) -> void
{
    auto const [ lower, upper ] = this->spectral_metric.threshold_limits( );
    ui->thresholdSpinBox->setRange( lower, upper );
}

auto MainWindow::setupClassifyColorTable(
    HINALEA_INOUT QImage & classifyImage
    ) -> void
{
    static auto constexpr colors = ::std::array{
        qRgba(   0,   0,   0,   0 ),    /* Transparent */
        qRgba( 255,   0,   0, 255 ),    /* Red */
        qRgba(   0, 255,   0, 255 ),    /* Green */
        qRgba(   0,   0, 255, 255 ),    /* Blue */
        /* etc */
        };

    classifyImage.setColorTable( { colors.begin( ), colors.end( ) } );
}

auto MainWindow::settingsPath(
    ) const -> ::hinalea::fs::path
{
    return ::pathCast( ui->settingsLineEdit );
}

auto MainWindow::whitePath(
    ) const -> ::hinalea::fs::path
{
    return ::pathCast( ui->whiteLineEdit );
}

auto MainWindow::darkPath(
    ) const -> ::hinalea::fs::path
{
    return ::pathCast( ui->darkLineEdit );
}

auto MainWindow::matrixPath(
    ) const -> ::hinalea::fs::path
{
    return ::pathCast( ui->matrixLineEdit );
}

auto MainWindow::gapPath(
    ) const -> ::hinalea::fs::path
{
    return ::pathCast( ui->gapLineEdit );
}

auto MainWindow::exposure(
    ) const -> ::hinalea::MicrosecondsI
{
    return ::exposureCast( ui->exposureSpinBox->value( ) );
}

auto MainWindow::gain(
    ) const -> ::hinalea::Real
{
    return ::gainCast( ui->gainSpinBox->value( ) );
}

auto MainWindow::gainMode(
    ) const -> ::hinalea::Int
{
    return static_cast< ::hinalea::Int >( ui->gainModeSpinBox->value( ) );
}

auto MainWindow::gapIndex(
    ) const -> ::hinalea::Size
{
    return ::gapIndexCast( ui->gapIndexSpinBox->value( ) );
}

auto MainWindow::whiteReflectance(
    ) const -> ::hinalea::Real
{
    return ::reflectanceCast( ui->reflectanceSpinBox->value( ) );
}

auto MainWindow::cameraType(
    ) const -> ::hinalea::CameraType
{
    return ::cameraTypes( ).value( ui->cameraComboBox->currentText( ) );
}

auto MainWindow::displayChannels(
    ) const -> ::hinalea::Int
{
    return ( this->camera.channels( ) == 3 )
        ? 4 // Add alpha channel for QImage::Format to work nicely with 16-bit RGB images.
        : 1
        ;
}

auto MainWindow::intensityThreshold(
    ) const -> ::hinalea::Int
{
    /* Note:
     * Some cameras do not actually go up to the theoretical max value.
     * You can add your own code to have it user defined.
     */
    return ( 1 << this->camera.bit_depth( ) ) - 1;
}

auto MainWindow::ignoreCount(
    ) const -> ::hinalea::Int
{
    /* If you wish to ignore saturated pixels you can add your own code. */
    return 0;
}

auto MainWindow::binning(
    ) const -> ::hinalea::Int
{
    auto const index = ui->binningComboBox->currentIndex( );
    return 1 << ( ( index + 1 ) / 2 );
}

auto MainWindow::binningMode(
    ) const -> ::hinalea::BinningModeVariant
{
    if ( auto const index = ui->binningComboBox->currentIndex( );
        ( index % 2 ) == 0 )
    {
        return ::hinalea::BinningMode::Average;
    }
    else
    {
        return ::hinalea::BinningMode::Sum;
    }
}

auto MainWindow::operationMode(
    ) const -> MainWindow::OperationMode
{
    return ( ui->modeComboBox->currentIndex( ) == 0 )
        ? OperationMode::StaticMode
        : OperationMode::RealtimeMode;
}

auto MainWindow::displayMode(
    ) const -> ::hinalea::Realtime::DisplayModeVariant
{
    return ::hinalea::DisplayMode::RawSelectedGap;
//    return ::hinalea::DisplayMode::RawEveryGap;
//    return ::hinalea::DisplayMode::ProcessedPseudoRgb;
}

auto MainWindow::realtimeMode(
    ) const -> ::hinalea::Realtime::RealtimeModeVariant
{
    return ::hinalea::RealtimeMode::ProcessedWavelength;
//    return ::hinalea::RealtimeMode::RawChannelSignals;
}

auto MainWindow::measurementType(
    ) const -> ::hinalea::Acquisition::MeasurementTypeVariant
{
    switch ( ui->measurementTypeComboBox->currentIndex( ) )
    {
        case 0: { return ::hinalea::MeasurementType::Raw;   }
        case 1: { return ::hinalea::MeasurementType::White; }
        case 2: { return ::hinalea::MeasurementType::Dark;  }
    }

    Q_UNREACHABLE( );
}

auto MainWindow::progressCallback(
    HINALEA_IN ::hinalea::Int const percent
    ) -> void
{
    Q_EMIT this->progressChanged( static_cast< int >( percent ) );
}

auto MainWindow::makeProgressCallback(
    ) -> ::hinalea::ProgressCallback
{
    QApplication::setOverrideCursor( Qt::BusyCursor );
    return
        [ this ]( ::hinalea::Int const percent )
        {
            Q_EMIT this->progressChanged( percent );
        };
}

auto MainWindow::xAxisTitle(
    ) const -> QString
{
    return ::std::visit(
        ::hinalea::overloaded{
            [ ]( ::hinalea::RealtimeMode::ProcessedWavelength_t )
            {
                return QObject::tr( "Wavelength (nm)" );
            },
            [ ]( ::hinalea::RealtimeMode::RawChannelSignals_t )
            {
                return QObject::tr( "Gaps" );
            },
            },
        this->realtimeMode( )
        );
}

auto MainWindow::yAxisTitle(
    ) const -> QString
{
    return this->realtime_reflectance_is_active( )
        ? QObject::tr( "Reflectance" )
        : QObject::tr( "Intensity" );
}

auto MainWindow::xAxisRange(
    ) const -> ::std::array< ::hinalea::Real, 2 >
{
    return ::std::visit(
        ::hinalea::overloaded{
            [ this ]( ::hinalea::RealtimeMode::ProcessedWavelength_t )
            {
                auto const wavelengths = this->realtime.band_wavelengths( );
                HINALEA_ASSERT( not wavelengths.empty( ) );
                return ::std::array{ wavelengths.front( ), wavelengths.back( ) };
            },
            [ this ]( ::hinalea::RealtimeMode::RawChannelSignals_t )
            {
                auto const indexes = this->realtime.gap_indexes( );
                HINALEA_ASSERT( not indexes.empty( ) );
                return ::std::array{
                    static_cast< ::hinalea::Real >( indexes.front( ) ),
                    static_cast< ::hinalea::Real >( indexes.back( ) )
                    };
            },
        },
        this->realtimeMode( )
        );
}

auto MainWindow::yAxisRange(
    ) const -> ::std::array< ::hinalea::Real, 2 >
{
    if ( this->realtime_reflectance_is_active( ) )
    {
        return { 0.0, 1.5 };
    }
    else
    {
        auto const upperBound = static_cast< ::hinalea::Real >( this->intensityThreshold( ) );
        return { 0.0, upperBound };
    }
}

auto MainWindow::powerOn(
    ) -> void
try
{
    QObject::connect(
        this->displayTimer,
        &QTimer::timeout,
        this,
        &MainWindow::onDisplayTimerTimeout
        );

    if ( auto const settings = this->settingsPath( );
         not ::hinalea::fs::exists( settings ) )
    {
        this->onPowerOnFailure( QObject::tr( "Settings path does not exist." ) );
        return;
    }

    if ( this->operationMode( ) == OperationMode::StaticMode )
    {
        if ( not this->powerOnAcquisition( ) )
        {
            return;
        }
    }
    else
    {
        if ( not this->powerOnRealtime( ) )
        {
            return;
        }
    }

    this->updateDark( );

    auto const rect = this->camera.qt_region_of_interest( );
    ui->imageView->scene( )->setSceneRect( rect );
    ui->imageView->fitInView( rect, Qt::KeepAspectRatio );

    this->displayItem->show( );
    this->displayItem->setPixmap( QPixmap{ this->camera.qt_size( ) } );

    this->classifyItem->show( );

    auto classifyImage = QImage{ this->camera.qt_size( ), QImage::Format_Indexed8 };
    this->setupClassifyColorTable( classifyImage );
    this->classifyItem->setPixmap( QPixmap::fromImage( ::std::move( classifyImage ) ) );

    if ( this->operationMode( ) == OperationMode::RealtimeMode )
    {
        this->realtimeThread = ::std::thread{
            [ this ]
            {
                try
                {
                    this->realtime.run( );
                }
                catch ( ::std::exception const & exc )
                {
                    Q_EMIT this->threadFailed( QObject::tr( "Realtime Error" ), QString{ exc.what( ) } );
                }
            }
            };
    }

    this->updateImageTimerInterval( );
    this->displayTimer->start( );

    this->enablePowerWidgets( true );
}
catch ( ::std::exception const & exc )
{
    ::hinalea::log::error( exc.what( ), __FILE__, __func__, __LINE__ );
    QMessageBox::critical( this, QObject::tr( "Error" ), exc.what( ) );
    this->powerOff( );
}

auto MainWindow::powerOff(
    ) -> void
{
    qDebug( ) << Q_FUNC_INFO;

    this->displayTimer->stop( );

    QObject::disconnect(
        this->displayTimer,
        &QTimer::timeout,
        this,
        &MainWindow::onDisplayTimerTimeout
        );

    this->displaySemaphore.acquire( );
    auto const releaser = QSemaphoreReleaser{ this->displaySemaphore };

    {
        auto const blocker = QSignalBlocker{ ui->powerButton };
        ui->powerButton->setChecked( false );
    }

    if ( this->acquisition.is_open( ) )
    {
        this->acquisition.cancel( );
        ::joinThread( this->recordThread );
        this->acquisition.close( );
    }
    else if ( this->realtime.is_open( ) )
    {
        this->realtime.cancel( );
        ::joinThread( this->realtimeThread );
        this->realtime.close( );
    }
    else
    {
        if constexpr ( ::hinalea_internal )
        {
            if ( this->camera.is_open( ) )
            {
                this->camera.close( );
            }
        }
    }

    this->displayItem->hide( );
    this->classifyItem->hide( );
    this->displayImage.reset( );
    this->enablePowerWidgets( false );

    for ( auto * const spinBox : ::std::initializer_list< QAbstractSpinBox * >{
        ui->minSpinBox,
        ui->maxSpinBox,
        ui->saturationSpinBox,
        ui->fpsSpinBox,
    } )
    {
        spinBox->setProperty( "value", spinBox->property( "minimum" ) );
    }
}

auto MainWindow::onPowerOnFailure(
    HINALEA_IN QString const & description
    ) -> void
{
    QMessageBox::critical( this, QObject::tr( "Error" ), description );
    this->powerOff( );
}

auto MainWindow::powerOnAcquisition(
    ) -> bool
{
    qDebug( ) << Q_FUNC_INFO;

    auto const onOpen =
        [ this ]
        {
            this->setupAll( );
            this->displayImage = this->camera.allocate_image( this->displayChannels( ) );
            this->camera.start_acquisition( );
        };

    if ( this->acquisition.open( this->settingsPath( ) ) )
    {
        onOpen( );
        return true;
    }

    if constexpr ( ::hinalea_internal ) /* Useful for testing cameras without FPI present. */
    {
        if ( this->camera.open( ) )
        {
            onOpen( );
            return true;
        }
    }

    this->onPowerOnFailure( QObject::tr( "Failed to power on static acquisition mode." ) );
    return false;
}

auto MainWindow::powerOnRealtime(
    ) -> bool
{
    qDebug( ) << Q_FUNC_INFO;

    if ( not this->realtime.open( this->settingsPath( ) ) )
    {
        this->onPowerOnFailure( QObject::tr( "Failed to power on realtime mode." ) );
        return false;
    }

    this->setupAll( );

    /* Realtime images are always 8-bit images with 3 channels. */
    this->displayImage = hinalea::make_aligned< ::std::byte[ ] >( this->camera.alignment( ), 3 * this->camera.area( ) );

    this->realtime.set_display_mode( this->displayMode( ) );
    this->realtime.set_selected_index( 0 );
    this->realtime.set_gap_path( this->gapPath( ) );
    this->realtime.set_matrix_path( this->matrixPath( ) );
    this->realtime.set_white_path( this->whitePath( ) );
    this->realtime.set_use_reflectance( ui->reflectanceCheckBox->isChecked( ) );
    this->realtime.set_classify_callback( this->classifyCallback_ );

    if ( not this->realtime.setup( this->realtimeMode( ) ) )
    {
        QMessageBox::critical( this, QObject::tr( "Error" ), QObject::tr( "Failed to setup realtime mode." ) );
        this->powerOff( );
        return false;
    }

    this->setupXAxis( );
    this->setupYAxis( );
    return true;
}

auto MainWindow::record(
    ) -> void
{
    ::joinThread( this->recordThread );
    this->setupAcquisition( );

    /* Format will be: YYYYMMDD_hhmmss. */
    auto datetime = QDateTime::currentDateTime( ).toString( Qt::ISODate );
    datetime.remove( ':' );
    datetime.remove( '-' );
    datetime.replace( 'T', '_' );

    auto id = datetime.toStdString( );

    auto const name = id + ::std::visit(
        ::hinalea::overloaded{
            [ ]( ::hinalea::MeasurementType::Raw_t       ){ return "";           },
            [ ]( ::hinalea::MeasurementType::White_t     ){ return "_white";     },
            [ ]( ::hinalea::MeasurementType::Dark_t      ){ return "_dark";      },
            [ ]( ::hinalea::MeasurementType::FlatField_t ){ return "_flatfield"; },
            },
        this->measurementType( )
        );

    auto saveDir = ::ioDir( ) / HINALEA_PATH( "raw" ) / name;

    auto const message = "Saving to: " + QString::fromStdString( saveDir.generic_string( ) );
    qInfo( ).noquote( ) << message;
    QMessageBox::information( this, QObject::tr( "Recording" ), message );

    this->enableRecordWidgets( false );
    this->isRecording = true;

    this->recordThread = ::std::thread{
        [ this, HINALEA_CAPTURE( saveDir ), HINALEA_CAPTURE( id ) ]
        {
            try
            {
                this->acquisition.record( saveDir, id, this->makeProgressCallback( ) );
            }
            catch ( ::std::exception const & exc )
            {
                Q_EMIT this->threadFailed( QObject::tr( "Record Error" ), QString{ exc.what( ) } );
            }
        }
        };
}

auto MainWindow::cancel(
    ) -> void
{
    if ( this->isRecording )
    {
        qInfo( ) << "Recording cancelled.";
    }

    this->acquisition.cancel( );
    this->realtime.cancel( );
}

auto MainWindow::process(
    ) -> void
{
    ::joinThread( this->processThread );
    auto const dir = QFileDialog::getExistingDirectory( this, QObject::tr( "Load raw data directory." ), ::pathCast( ::ioDir( ) / HINALEA_PATH( "raw" ) ) );

    if ( dir.isEmpty( ) )
    {
        return;
    }

    if ( dir.endsWith( "_dark" ) )
    {
        QMessageBox::information( this, QObject::tr( "Process Information" ), QObject::tr( "Dark data does not need to be processed." ) );
        return;
    }

    this->setupProcess( );

    auto rawDir = ::hinalea::fs::path{ dir.toStdString( ) };
    auto processDir = ::ioDir( ) / HINALEA_PATH( "processed" ) / rawDir.filename( );

    auto const message = "Processing from: " + QString::fromStdString( rawDir.generic_string( ) )
                       + "\nProcessing to: " + QString::fromStdString( processDir.generic_string( ) );
    qInfo( ).noquote( ) << message;
    QMessageBox::information( this, QObject::tr( "Processing" ), message );

    this->enableProcessWidgets( false );
    this->isProcessing = true;

    this->processThread = ::std::thread{
        [ this, HINALEA_CAPTURE( rawDir ), HINALEA_CAPTURE( processDir ) ]
        {
            try
            {
                this->processor.process( rawDir, processDir, this->makeProgressCallback( ) );
            }
            catch ( ::std::exception const & exc )
            {
                Q_EMIT this->threadFailed( QObject::tr( "Process Error" ), QString{ exc.what( ) } );
            }
        }
        };
}

auto MainWindow::allSeries(
    ) const -> QVector< QLineSeries * >
{
    return { this->seriesL, this->seriesR, this->seriesG, this->seriesB };
}

auto MainWindow::setupAxis(
    HINALEA_IN Qt::Orientation const                    orientation,
    HINALEA_IN QString const &                          title,
    HINALEA_IN ::std::array< ::hinalea::Real, 2 > const values
    ) -> void
{
    HINALEA_ASSERT( not values.empty( ) );
    auto const axes = this->chart->axes( orientation );
    auto * const axis = axes[ 0 ];
    axis->setRange( values.front( ), values.back( ) );
    axis->setTitleText( title );
}

auto MainWindow::setupXAxis(
    ) -> void
{
    this->setupAxis( Qt::Horizontal, this->xAxisTitle( ), this->xAxisRange( ) );
}

auto MainWindow::setupYAxis(
    ) -> void
{
    this->setupAxis( Qt::Vertical, this->yAxisTitle( ), this->yAxisRange( ) );
}

auto MainWindow::setupAcquisition(
    ) -> void
{
    this->acquisition.set_file_format( ::hinalea::FileFormat::Png );
    this->acquisition.set_measurement_type( this->measurementType( ) );
    this->acquisition.set_white_reflectance( this->whiteReflectance( ) );
}

auto MainWindow::setupProcess(
    ) -> void
{
    if ( not ::hinalea::fs::is_directory( this->whitePath( ) ) )
    {
        this->processor.set_cube_type( ::hinalea::CubeType::Intensity );
    }
    else
    {
        this->processor.set_cube_type( ::hinalea::CubeType::Intensity | ::hinalea::CubeType::Reflectance );
    }

    this->processor.set_data_type( ::hinalea::DataType::Float32 );
    // this->processor.set_scale_factor( ::hinalea::ndebug ? 0.5 : 0.1 ); /* make processing faster for debugging purposes */
    this->processor.set_scale_factor( 1.0 );
    this->processor.set_smooth_size( ui->smoothSpinBox->value( ) );
    this->processor.set_settings_path( this->settingsPath( ) );

    this->processor.set_suffix( ::hinalea::CubeType::Intensity  , HINALEA_PATH( "" ) );
    this->processor.set_suffix( ::hinalea::CubeType::Reflectance, HINALEA_PATH( "_ref" ) );
}

auto MainWindow::setupBitDepth(
    ) -> void
{
    auto const bitDepths = this->camera.valid_bit_depths( );
    auto const bitDepth = 8 * ( ui->bitDepthComboBox->currentIndex( ) + 1 );

    if ( ::std::find( bitDepths.begin( ), bitDepths.end( ), bitDepth ) != bitDepths.end( ) )
    {
        this->camera.set_bit_depth( bitDepth );
    }
    else
    {
        this->camera.set_bit_depth( bitDepths.front( ) );
        auto const message = QObject::tr( "Could not set bit depth to: %0." ).arg( bitDepth );
        qWarning( ) << Q_FUNC_INFO << message;
        QMessageBox::warning( this, QObject::tr( "Invalid Bit Depth" ), message );
    }
}

auto MainWindow::setupExposure(
    ) -> void
{
    auto const uiCast =
        [ ]( ::hinalea::MicrosecondsI const usec )
        {
            if constexpr ( ::ui_exposure_is_milliseconds )
            {
                return ::std::chrono::duration_cast< ::hinalea::MillisecondsI >( usec );
            }
            else
            {
                return usec;
            }
        };

    auto const [ lowerExposure, upperExposure ] = this->camera.exposure_limits( );
    auto const minExposure = qMax( uiCast( lowerExposure ), ::UiExposure{ 1 } );
    auto const maxExposure = qMin( uiCast( upperExposure ), ::std::chrono::duration_cast< ::UiExposure >( ::hinalea::MillisecondsI{ 500 } ) );
    ui->exposureSpinBox->setRange( minExposure.count( ), maxExposure.count( ) );
    this->camera.set_exposure( this->exposure( ) );
}

auto MainWindow::setupGain(
    ) -> void
{
    auto const [ lowerGain, upperGain ] = this->camera.gain_limits( );
    auto const minGain = static_cast< int >( lowerGain );
    auto const maxGain = static_cast< int >( upperGain );
    ui->gainSpinBox->setRange( minGain, maxGain );
    this->camera.set_gain( this->gain( ) );
}

auto MainWindow::setupGainMode(
    ) -> void
{
    auto const [ lowerMode, upperMode ] = this->camera.gain_mode_limits( );
    auto const minMode = static_cast< int >( lowerMode );
    auto const maxMode = static_cast< int >( upperMode );
    ui->gainModeSpinBox->setRange( minMode, maxMode );
    this->camera.set_gain_mode( this->gainMode( ) );
}

auto MainWindow::setupGapIndex(
    ) -> void
{
    auto const gapIndexes = this->fpi.gap_indexes( );

    if ( gapIndexes.empty( ) )
    {
        qWarning( ) << Q_FUNC_INFO << "Gap indexes are empty.";
        ui->gapIndexSpinBox->setRange( 0, 0 );
    }
    else
    {
        auto const minGapIndex = static_cast< int >( gapIndexes.front( ) );
        auto const maxGapIndex = static_cast< int >( gapIndexes.back( ) );
        ui->gapIndexSpinBox->setRange( minGapIndex, maxGapIndex );
        this->fpi.set_gap_index( this->gapIndex( ) );
    }
}

auto MainWindow::setupBinning(
    ) -> void
{
    Q_ASSERT( not this->camera.is_acquiring( ) );
    bool ok = true;

    auto const bin = this->binning( );
    ok = ok and this->camera.set_binning( ::hinalea::Orientation::Horizontal, bin );
    ok = ok and this->camera.set_binning( ::hinalea::Orientation::Vertical  , bin );

    auto const mode = this->binningMode( );
    ok = ok and this->camera.set_binning_mode( ::hinalea::Orientation::Horizontal, mode );
    ok = ok and this->camera.set_binning_mode( ::hinalea::Orientation::Vertical  , mode );

    if ( not ok )
    {
        qWarning( ) << "Failed to setup binning.";
    }
}

auto MainWindow::setupAll(
    ) -> void
{
    this->setupBinning( );
    this->setupBitDepth( );
    this->setupExposure( );
    this->setupGain( );
    this->setupGainMode( );
    this->setupGapIndex( );
}

auto MainWindow::finishRecord(
    ) -> void
{
    auto const blocker = QSignalBlocker{ ui->recordButton };
    ui->recordButton->setChecked( false );
    ::joinThread( this->recordThread );
    this->enableRecordWidgets( true );
    qInfo( ) << "Recording finished.";
}

auto MainWindow::finishProcess(
    ) -> void
{
    ::joinThread( this->processThread );
    this->enableProcessWidgets( true );
    qInfo( ) << "Processing finished.";
}

auto MainWindow::updateCameraType(
    ) -> void
{
    this->camera = ::hinalea::Camera{ this->cameraType( ) };
    this->realtime = ::hinalea::Realtime{ this->camera, this->fpi };
    this->acquisition = ::hinalea::Acquisition{ this->camera, this->fpi };
    this->updateDark( );
}

auto MainWindow::updateWhite(
    ) -> void
{
    this->processor.set_white_path( this->whitePath( ) );
}

auto MainWindow::updateDark(
    ) -> void
{
    this->acquisition.set_dark_path( this->darkPath( ) );
}

template < >
auto MainWindow::updateSeries< ::hinalea::RealtimeMode::ProcessedWavelength_t >(
    ) -> void
{
    auto const & location = *this->endmemberLocation_;
    auto const spectra = this->realtime.spectra< qreal >( location.y( ), location.x( ) );
    auto const wavelengths = this->realtime.band_wavelengths( );
    auto const count = wavelengths.size( );

    for ( auto i = ::std::size_t{ 0 }; i < count; ++i )
    {
        this->seriesL->append( wavelengths[ i ], spectra[ i ] );
    }

#if 0
   qDebug( ) << "L:" << seriesL->points( );
   qDebug( ) << "================================================================================================================================";
#endif
}

template < >
auto MainWindow::updateSeries< ::hinalea::RealtimeMode::RawChannelSignals_t >(
    ) -> void
{
    auto const & location = *this->endmemberLocation_;
    auto const spectra = this->realtime.spectra< qreal >( location.y( ), location.x( ) );
    auto const gap_indexes = this->realtime.gap_indexes( );
    auto const count = gap_indexes.size( );

    if ( auto const channels = this->camera.channels( );
         channels == 1 )
    {
        for ( auto i = ::std::size_t{ 0 }; i < count; ++i )
        {
            this->seriesL->append( gap_indexes[ i ], spectra[ i ] );
        }

#if 0
       qDebug( ) << "L:" << seriesL->points( );
       qDebug( ) << "================================================================================================================================";
#endif
    }
    else
    {
        HINALEA_ASSERT( channels == 3 );

        for ( auto i = ::std::size_t{ 0 }; i < count; ++i )
        {
            auto const x = gap_indexes[ i ];
            this->seriesR->append( x, spectra[ i + count * 0 ] );
            this->seriesG->append( x, spectra[ i + count * 1 ] );
            this->seriesB->append( x, spectra[ i + count * 2 ] );
        }

#if 0
        qDebug( ) << "R:" << seriesR->points( );
        qDebug( ) << "G:" << seriesG->points( );
        qDebug( ) << "B:" << seriesB->points( );
        qDebug( ) << "================================================================================================================================";
#endif
    }
}

auto MainWindow::onUpdateSeries(
    ) -> void
{
    for ( auto * const series : this->allSeries( ) )
    {
        series->clear( );
    }

    if ( this->endmemberLocation_.has_value( ) )
    {
        ::std::visit(
            [ this ]( auto && realtime_mode )
            {
                using RealtimeMode = HINALEA_TYPEOF( realtime_mode );
                this->updateSeries< RealtimeMode >( );
            },
            this->realtime.realtime_mode( )
            );
    }
}

auto MainWindow::updateImageTimerInterval(
    ) -> void
{
    using namespace ::std::chrono_literals;
    auto const interval = qMax( 30ms, ::std::chrono::ceil< ::std::chrono::milliseconds >( this->exposure( ) ) );
    this->displayTimer->setInterval( interval );
}

auto MainWindow::updateAcquisitionImage(
    ) -> void
try
{
    auto releaser = QSemaphoreReleaser{ this->displaySemaphore };

    /* Raw images are always monochrome, so allocate only 1 channel. */
    auto rawImage = this->camera.allocate_image( 1 );

    /* Do not use Camera::image instead of Acquisition::image since the
     * Acquisition class does extra internal synchronizations.
     */
    if ( not this->acquisition.image( rawImage ) )
    {
        return;
    }

    {
        auto const [ min, max, saturation ] = hinalea::image_statistics( this->camera.qt_image( rawImage ), this->intensityThreshold( ), this->ignoreCount( ) );
        auto const fps = this->camera.frames_per_second( );
        Q_EMIT this->doUpdateStatistics( min, max, saturation, fps );
    }

    auto const channels = this->displayChannels( );

    if ( channels == 1 )
    {
        /* Monochrome sensor; no processing required. */
        this->displayImage = ::std::move( rawImage );
    }
    else
    {
        /* RGB sensor, need to convert monochrome color filter array into RGBA image. */
        ::hinalea::demosaic( this->camera, rawImage, this->displayImage, channels );
    }

    releaser.cancel( );
    Q_EMIT this->doUpdateImage( );
}
catch ( ::std::exception const & exc )
{
    ::std::cerr << exc.what( ) << '\n';
}

auto MainWindow::updateRealtimeImage(
    ) -> void
{
    auto releaser = QSemaphoreReleaser{ this->displaySemaphore };
    this->displayImage = this->realtime.allocate_image( );

    if ( not this->realtime.image( this->displayImage ) )
    {
        return;
    }

    {
        auto const [ min, max ] = this->realtime.min_max_values( );
        // auto const fps = this->camera.frames_per_second( );
        auto const fps = this->realtime.actual_fps( );
        Q_EMIT this->doUpdateStatistics( min, max, ui->saturationSpinBox->minimum( ), fps );
    }

    releaser.cancel( );
    Q_EMIT this->doUpdateImage( );
    Q_EMIT this->doUpdateSeries( );
}

auto MainWindow::realtime_reflectance_is_active(
    ) const -> bool
{
    return ui->reflectanceCheckBox->isChecked( ) and this->realtime.is_white_compatible( );
}

auto MainWindow::enablePowerWidgets(
    HINALEA_IN bool const enable
    ) -> void
{
    for ( auto * const widget : ::std::initializer_list< QWidget * >{
        ui->exposureGroupBox,
        ui->gainGroupBox,
        ui->gapIndexGroupBox,
    } )
    {
        widget->setEnabled( enable );
    }

    ui->recordButton->setEnabled( enable and ( this->operationMode( ) == OperationMode::StaticMode ) );

    for ( auto * const widget : ::std::initializer_list< QWidget * >{
        ui->binningGroupBox,
        ui->bitDepthGroupBox,
        ui->cameraComboBox,
        ui->loadSettingsButton,
        ui->loadWhiteButton,
        ui->processButton,
        ui->smoothSpinBox,
    } )
    {
        widget->setDisabled( enable );
    }
}

auto MainWindow::enableRecordWidgets(
    HINALEA_IN bool const enable
    ) -> void
{
    for ( auto * const widget : ::std::initializer_list< QWidget * >{
        ui->powerButton,
        ui->exposureSpinBox,
        ui->gainSpinBox,
        ui->gainModeSpinBox,
        ui->gapIndexSpinBox,
        ui->loadDarkButton,
        ui->reflectanceSpinBox,
    } )
    {
        widget->setEnabled( enable );
    }
}

auto MainWindow::enableProcessWidgets(
    HINALEA_IN bool const enable
    ) -> void
{
    for ( auto * const widget : ::std::initializer_list< QWidget * >{
        ui->cameraComboBox,
        ui->loadSettingsButton,
        ui->powerButton,
        ui->processButton,
        ui->loadWhiteButton,
        ui->smoothSpinBox,
    } )
    {
        widget->setEnabled( enable );
    }
}

auto MainWindow::onUpdateImage(
    ) -> void
{
    auto const releaser = QSemaphoreReleaser{ this->displaySemaphore };
    auto const channels = this->displayChannels( );
    auto qImage = this->camera.qt_image( this->displayImage, channels );
    this->displayItem->setPixmap( QPixmap::fromImage( ::std::move( qImage ) ) );
}

auto MainWindow::onUpdateStatistics(
    HINALEA_IN int const    min,
    HINALEA_IN int const    max,
    HINALEA_IN int const    saturation,
    HINALEA_IN double const fps
    ) -> void
{
    ui->minSpinBox->setValue( min );
    ui->maxSpinBox->setValue( max );
    ui->saturationSpinBox->setValue( saturation );
    ui->fpsSpinBox->setValue( fps );
}

auto MainWindow::onDisplayTimerTimeout(
    ) -> void
{
    if ( not this->displaySemaphore.tryAcquire( ) )
    {
        return;
    }

    auto releaser = QSemaphoreReleaser{ this->displaySemaphore };
    ::joinThread( this->displayThread );

    this->displayThread = ::std::thread(
        [ this ]
        {
            if ( this->realtime.is_active( ) )
            {
                this->updateRealtimeImage( );
            }
            else
            {
                this->updateAcquisitionImage( );
            }
        }
        );

    releaser.cancel( );
}

auto MainWindow::onPowerButtonToggled(
    HINALEA_IN bool const checked
    ) -> void
{
    QApplication::setOverrideCursor( Qt::BusyCursor );

    if ( checked )
    {
        this->powerOn( );
    }
    else
    {
        this->powerOff( );
    }

    QApplication::restoreOverrideCursor( );
}

auto MainWindow::onRecordButtonToggled(
    HINALEA_IN bool const checked
    ) -> void
{
    if ( checked )
    {
        this->record( );
    }
    else
    {
        this->cancel( );
    }
}

auto MainWindow::onProcessButtonClicked(
    ) -> void
{
    this->process( );
}

auto MainWindow::onCameraComboBoxCurrentIndexChanged(
    HINALEA_IN int const index
    ) -> void
{
    HINALEA_UNUSED( index );
    this->updateCameraType( );
}

auto MainWindow::onHorizontalCheckBoxToggled(
    HINALEA_IN bool const checked
    ) -> void
{
    if ( this->camera.is_open( ) )
    {
        if ( not this->camera.set_flip( ::hinalea::Orientation::Horizontal, checked ) )
        {
            qWarning( ) << "Failed to change horizontal flip:" << checked;
        }
    }
}

auto MainWindow::onVerticalCheckBoxToggled(
    HINALEA_IN bool const checked
    ) -> void
{
    if ( this->camera.is_open( ) )
    {
        if ( not this->camera.set_flip( ::hinalea::Orientation::Vertical, checked ) )
        {
            qWarning( ) << "Failed to change vertical flip:" << checked;
        }
    }
}

auto MainWindow::onExposureSpinBoxValueChanged(
    HINALEA_IN int const value
    ) -> void
{
    bool ok = true;

    if ( auto const exposure = ::exposureCast( value );
         this->realtime.is_open( ) )
    {
        ok = this->realtime.set_exposure( exposure );
    }
    else if ( this->camera.is_open( ) )
    {
        ok = this->camera.set_exposure( exposure );
    }

    if ( ok )
    {
        this->updateImageTimerInterval( );
    }
    else
    {
        qWarning( ) << "Failed to change exposure.";
    }
}

auto MainWindow::onGainSpinBoxValueChanged(
    HINALEA_IN int const value
    ) -> void
{
    bool ok = true;

    if ( auto const gain = ::gainCast( value );
         this->realtime.is_open( ) )
    {
        ok = this->realtime.set_gain( gain );
    }
    else if ( this->camera.is_open( ) )
    {
        ok = this->camera.set_gain( gain );
    }

    if ( not ok )
    {
        qWarning( ) << "Failed to change gain.";
    }
}

auto MainWindow::onGainModeSpinBoxValueChanged(
    HINALEA_IN int const value
    ) -> void
{
    if ( bool const ok = this->camera.set_gain_mode( value );
         not ok )
    {
        qWarning( ) << "Failed to change gain mode.";
    }
}

auto MainWindow::onGapIndexSpinBoxValueChanged(
    HINALEA_IN int const value
    ) -> void
{
    if ( auto const gapIndex = ::gapIndexCast( value );
         this->realtime.is_open( ) )
    {
        if ( auto const indexes = this->realtime.gap_indexes< QVector< ::hinalea::Size > >( );
             indexes.contains( gapIndex ) )
        {
            this->realtime.set_selected_index( gapIndex );
        }
        else
        {
            qInfo( ) << "Did not set realtime selected index to" << gapIndex << "since it is not in the loaded gap index list.";
        }
    }
    else if ( this->fpi.is_open( ) )
    {
        this->fpi.set_gap_index( gapIndex );
    }
}

auto MainWindow::onRefletanceSpinBoxValueChanged(
    HINALEA_IN double const value
    ) -> void
{
    if ( this->acquisition.is_open( ) )
    {
        this->acquisition.set_white_reflectance( ::reflectanceCast( value ) );
    }
}

auto MainWindow::onThresholdSpinBoxValueChanged(
    HINALEA_IN double const value
    ) -> void
{
    HINALEA_UNUSED( value );
}

auto MainWindow::onLoadSettingsClicked(
    ) -> void
{
    if ( auto dir = QFileDialog::getExistingDirectory( this, QObject::tr( "Load FPI settings directory." ) );
         not dir.isEmpty( ) )
    {
        /* NOTE:
         * We support two settings formats. They are usually as follows:
         * 1) "./db/settings" file
         * 2) "./calib/SENSOR_NAME" directory
         */
        if ( auto const settings = ::hinalea::fs::path{ dir.toStdString( ) } / HINALEA_PATH( "settings" );
            ::hinalea::fs::is_regular_file( settings ) )
        {
            dir += "/settings";
        }

        ui->settingsLineEdit->setText( dir );
    }
}

auto MainWindow::onLoadWhiteClicked(
    ) -> void
{
    if ( auto const dir = QFileDialog::getExistingDirectory( this, QObject::tr( "Load processed white directory." ) );
         not dir.isEmpty( ) )
    {
        ui->whiteLineEdit->setText( dir );
        this->updateWhite( );
    }
}

auto MainWindow::onLoadDarkClicked(
    ) -> void
{
    if ( auto const dir = QFileDialog::getExistingDirectory( this, QObject::tr( "Load raw dark directory." ) );
         not dir.isEmpty( ) )
    {
        ui->darkLineEdit->setText( dir );
        this->updateDark( );
    }
}

auto MainWindow::onLoadMatrixClicked(
    ) -> void
{
    if ( auto const dir = QFileDialog::getExistingDirectory( this, QObject::tr( "Load trained realtime matrix directory." ) );
         not dir.isEmpty( ) )
    {
        ui->matrixLineEdit->setText( dir );
    }
}

auto MainWindow::onLoadGapClicked(
    ) -> void
{
    if ( auto const txt = QFileDialog::getOpenFileName( this, QObject::tr( "Load gap text file." ), { }, QObject::tr( "Text (*.txt)" ) );
         not txt.isEmpty( ) )
    {
        ui->gapLineEdit->setText( txt );
    }
}

auto MainWindow::onClearSettingsClicked(
    ) -> void
{
    ui->settingsLineEdit->clear( );
}

auto MainWindow::onClearWhiteClicked(
    ) -> void
{
    ui->whiteLineEdit->clear( );
}

auto MainWindow::onClearDarkClicked(
    ) -> void
{
    ui->darkLineEdit->clear( );
}

auto MainWindow::onClearMatrixClicked(
    ) -> void
{
    ui->matrixLineEdit->clear( );
}

auto MainWindow::onClearGapClicked(
    ) -> void
{
    ui->gapLineEdit->clear( );
}

auto MainWindow::onReflectanceCheckBoxToggled(
    HINALEA_IN bool const checked
    ) -> void
{
    this->realtime.set_use_reflectance( checked );
    this->setupYAxis( );
}

auto MainWindow::onProgressChanged(
    HINALEA_IN int const percent
    ) -> void
{
    ui->progressBar->setValue( percent );

    if ( percent == 100 )
    {
        if ( ::std::exchange( this->isRecording, false ) )
        {
            this->finishRecord( );
        }

        if ( ::std::exchange( this->isProcessing, false ) )
        {
            this->finishProcess( );
        }

        QApplication::restoreOverrideCursor( );
    }
}

auto MainWindow::onThreadFailed(
    HINALEA_IN QString const & title,
    HINALEA_IN QString const & what
    ) -> void
{
    qCritical( ) << what;
    QMessageBox::critical( this, title, what );
}

auto MainWindow::classifyCallback(
    HINALEA_IN ::hinalea::DataCube const & data_cube,
    HINALEA_IN void const * const          endmembers,
    HINALEA_IN ::hinalea::Int const        observations
    ) -> void
{
    using T = HINALEA_TYPEOF( this->spectral_metric )::value_type;

    HINALEA_ASSERT_MSG(
        "The data cube and the spectral metric data types do not match.",
        ::std::holds_alternative< ::hinalea::make_data_type_t< T > >( data_cube.data_type( ) )
        );

    HINALEA_ASSERT_MSG(
        "The data cube does not have BSQ layout.",
        ::std::holds_alternative< ::hinalea::Interleave::Bsq_t >( data_cube.interleave( ) )
        );

    auto const & spatial = data_cube.spatial;
    auto const bands = spatial.bands( );
    auto const area  = spatial.area( );

    auto const cast =
        [ ]( void const * ptr )
        {
            return ::hinalea::non_null{ static_cast< T const * >( ptr ) };
        };

    auto const X = ::hinalea::Matrix{ cast( data_cube.data( ) ), bands, area, true };
    auto const Y = ::hinalea::Matrix{ cast( endmembers ), observations, bands, false };

    this->spectral_metric.fit( X, Y );
    this->spectral_metric.classify( ui->thresholdSpinBox->value( ) );
    auto const classes = this->spectral_metric.classes( );
    auto const qSize = this->camera.qt_size( );

    auto classifyImage = QImage{
        classes.data( ),
        qSize.width( ),
        qSize.height( ),
        qSize.width( ),
        QImage::Format_Indexed8
        };

    this->setupClassifyColorTable( classifyImage );
    this->classifyItem->setPixmap( QPixmap::fromImage( ::std::move( classifyImage ) ) );
}

auto MainWindow::mousePressEvent(
    HINALEA_IN QMouseEvent * const event
    ) -> void
{
    QMainWindow::mousePressEvent( event );

    #if ( QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 ) )
    auto const globalPos = event->globalPosition( ).toPoint( );
    #else /* ^^^ Qt6 | Qt5 vvv */
    auto const globalPos = event->globalPos( );
    #endif /* QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 ) */

//    auto const imageViewPos = ui->imageView->viewport( )->mapFromGlobal( globalPos );
    auto const imageViewPos = ui->imageView->mapFromGlobal( globalPos );
    auto const scenePos = ui->imageView->mapToScene( imageViewPos ).toPoint( );

    if ( ui->imageView->sceneRect( ).contains( scenePos ) )
    {
        qDebug( ) << Q_FUNC_INFO << scenePos;
        HINALEA_ASSERT( scenePos.x( ) >= 0 );
        HINALEA_ASSERT( scenePos.y( ) >= 0 );
        HINALEA_ASSERT( scenePos.x( ) < this->camera.width( ) );
        HINALEA_ASSERT( scenePos.y( ) < this->camera.height( ) );
        this->endmemberLocation_ = scenePos;
        this->realtime.set_endmember_location( scenePos );
    }
    else
    {
        this->endmemberLocation_ = ::std::nullopt;
    }
}