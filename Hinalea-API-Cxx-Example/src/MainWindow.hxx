#pragma once

#include <Hinalea.h>

#include <QChartGlobal>
#include <QMainWindow>
#include <QSemaphore>

#include <optional>
#include <thread>

#ifdef HINALEA_INTERNAL
inline auto constexpr hinalea_internal = true;
#else /* HINALEA_INTERNAL */
inline auto constexpr hinalea_internal = false;
#endif /* HINALEA_INTERNAL */

/* QtCharts version 5 uses QtCharts namespace whereas QtCharts version 6 uses the default Qt namespace */
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
QT_BEGIN_NAMESPACE
class QChart;
class QLineSeries;
QT_END_NAMESPACE
#else /* Qt6 ^ | v Qt5 */
QT_CHARTS_BEGIN_NAMESPACE
class QChart;
class QLineSeries;
QT_CHARTS_END_NAMESPACE
QT_CHARTS_USE_NAMESPACE
#endif /* QT_VERSION_CHECK */

QT_BEGIN_NAMESPACE
class QGraphicsPixmapItem;
class QImage;
class QTimer;
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
QT_USE_NAMESPACE

/* The UI is set to show milliseconds by default. If you wish to use microseconds instead, change the value to `false`. */
inline bool constexpr ui_exposure_is_milliseconds = true;

using UiExposure = ::std::conditional_t<
    ::ui_exposure_is_milliseconds,
    ::hinalea::MillisecondsI,
    ::hinalea::MicrosecondsI
    >;

class MainWindow
    : public QMainWindow
{
    Q_OBJECT

public:
    enum OperationMode { StaticMode, RealtimeMode };
    Q_ENUM( OperationMode );

    explicit
    MainWindow(
        HINALEA_IN_OPT QWidget * parent = nullptr
        );

    ~MainWindow(
        );

    /* NOTE:
     * Using "void func( )" syntax instead of "auto func( ) -> void"
     * since Qt5 MOC does not work with trailing return type syntax.
     */
Q_SIGNALS:
    void progressChanged(
        HINALEA_IN int percent
        );

    void threadFailed(
        HINALEA_IN QString title,
        HINALEA_IN QString what
        );

    void doUpdateImage(
        );

    void doUpdateSeries(
        );

    void doUpdateStatistics(
        HINALEA_IN int    min,
        HINALEA_IN int    max,
        HINALEA_IN int    saturation,
        HINALEA_IN double fps
        );

private:
    QScopedPointer< Ui::MainWindow > ui;
    QTimer * displayTimer;
    QGraphicsPixmapItem * displayItem;
    QGraphicsPixmapItem * classifyItem;
    QChart * chart;
    QLineSeries * seriesL; // raw signal luminosity (monochrome) -- also used for processed wavelength mode
    QLineSeries * seriesR; // raw signal red
    QLineSeries * seriesG; // raw signal green
    QLineSeries * seriesB; // raw signal blue

    ::hinalea::Camera camera{ };
    ::hinalea::Fpi fpi{ };
    ::hinalea::Acquisition acquisition{ this->camera, this->fpi };
    ::hinalea::Processor processor{ };
    ::hinalea::Realtime realtime{ this->camera, this->fpi };
    ::hinalea::SpectralMetric< ::hinalea::f32 > spectral_metric{ ::hinalea::SpectralMetricType::SpectralAngle };

    ::std::optional< QPoint > endmemberLocation_{ ::std::nullopt };

    ::hinalea::Camera::Image displayImage{ };
    QSemaphore displaySemaphore{ 1 };

    ::std::mutex displayMutex{ };

    ::std::thread displayThread{ };
    ::std::thread recordThread{ };
    ::std::thread processThread{ };
    ::std::thread realtimeThread{ };

    bool isRecording{ false };
    bool isProcessing{ false };

    auto loadSettings(
        ) -> void;

    auto saveSettings(
        ) const -> void;

    auto initConnections(
        ) -> void;

    auto initImageView(
        ) -> void;

    auto initChartView(
        ) -> void;

    auto initSpectralMetric(
        ) -> void;

    auto setupClassifyColorTable(
        HINALEA_INOUT QImage & classifyImage
        ) -> void;

    [[ nodiscard ]]
    auto settingsPath(
        ) const -> ::hinalea::fs::path;

    [[ nodiscard ]]
    auto whitePath(
        ) const -> ::hinalea::fs::path;

    [[ nodiscard ]]
    auto darkPath(
        ) const -> ::hinalea::fs::path;

    [[ nodiscard ]]
    auto matrixPath(
        ) const -> ::hinalea::fs::path;

    [[ nodiscard ]]
    auto gapPath(
        ) const -> ::hinalea::fs::path;

    [[ nodiscard ]]
    auto exposure(
        ) const -> ::hinalea::MicrosecondsI;

    [[ nodiscard ]]
    auto gain(
        ) const -> ::hinalea::Real;

    [[ nodiscard ]]
    auto gainMode(
        ) const -> ::hinalea::Int;

    [[ nodiscard ]]
    auto gapIndex(
        ) const -> ::hinalea::Size;

    [[ nodiscard ]]
    auto whiteReflectance(
        ) const -> ::hinalea::Real;

    [[ nodiscard ]]
    auto cameraType(
        ) const -> ::hinalea::CameraType;

    [[ nodiscard ]]
    auto displayChannels(
        ) const -> ::hinalea::Int;

    [[ nodiscard ]]
    auto intensityThreshold(
        ) const -> ::hinalea::Int;

    [[ nodiscard ]]
    auto ignoreCount(
        ) const -> ::hinalea::Int;

    [[ nodiscard ]]
    auto binning(
        ) const -> ::hinalea::Int;

    [[ nodiscard ]]
    auto binningMode(
        ) const -> ::hinalea::BinningModeVariant;

    [[ nodiscard ]]
    auto operationMode(
        ) const -> MainWindow::OperationMode;

    [[ nodiscard ]]
    auto displayMode(
        ) const -> ::hinalea::Realtime::DisplayModeVariant;

    [[ nodiscard ]]
    auto realtimeMode(
        ) const -> ::hinalea::Realtime::RealtimeModeVariant;

    [[ nodiscard ]]
    auto measurementType(
        ) const -> ::hinalea::Acquisition::MeasurementTypeVariant;

    [[ nodiscard ]]
    auto horizontalFlip(
        ) const -> bool;

    [[ nodiscard ]]
    auto verticalFlip(
        ) const -> bool;

    auto progressCallback(
        HINALEA_IN ::hinalea::Int percent
        ) -> void;

    [[ nodiscard ]]
    auto makeProgressCallback(
        ) -> ::hinalea::ProgressCallback;

    [[ nodiscard ]]
    auto xAxisTitle(
        ) const -> QString;

    [[ nodiscard ]]
    auto yAxisTitle(
        ) const -> QString;

    [[ nodiscard ]]
    auto xAxisRange(
        ) const -> ::std::array< ::hinalea::Real, 2 >;

    [[ nodiscard ]]
    auto yAxisRange(
        ) const -> ::std::array< ::hinalea::Real, 2 >;

    auto powerOn(
        ) -> void;

    auto powerOff(
        ) -> void;

    auto onPowerOnFailure(
        HINALEA_IN QString const & description
        ) -> void;

    [[ nodiscard ]]
    auto powerOnAcquisition(
        ) -> bool;

    [[ nodiscard ]]
    auto powerOnRealtime(
        ) -> bool;

    auto record(
        ) -> void;

    auto cancel(
        ) -> void;

    auto process(
        ) -> void;

    auto allSeries(
        ) const -> QVector< QLineSeries * >;

    auto setupAxis(
        HINALEA_IN Qt::Orientation                    orientation,
        HINALEA_IN QString const &                    title,
        HINALEA_IN ::std::array< ::hinalea::Real, 2 > values
        ) -> void;

    auto setupXAxis(
        ) -> void;

    auto setupYAxis(
        ) -> void;

    auto setupAcquisition(
        ) -> void;

    auto setupProcess(
        ) -> void;

    auto setupBitDepth(
        ) -> void;

    auto setupExposure(
        ) -> void;

    auto setupGain(
        ) -> void;

    auto setupGainMode(
        ) -> void;

    auto setupGapIndex(
        ) -> void;

    auto setupBinning(
        ) -> void;

    auto setupFlip(
        ) -> void;

    auto setupAll(
        ) -> void;

    auto finishRecord(
        ) -> void;

    auto finishProcess(
        ) -> void;

    auto updateCameraType(
        ) -> void;

    auto updateWhite(
        ) -> void;

    auto updateDark(
        ) -> void;

    template <
        typename RealtimeMode
        >
    auto updateSeries(
        ) -> void;

    auto onUpdateSeries(
        ) -> void;

    auto updateImageTimerInterval(
        ) -> void;

    auto updateAcquisitionImage(
        ) -> void;

    auto updateRealtimeImage(
        ) -> void;

    auto realtimeReflectanceIsActive(
        ) const -> bool;

    auto enablePowerWidgets(
        HINALEA_IN bool enable
        ) -> void;

    auto enableRecordWidgets(
        HINALEA_IN bool enable
        ) -> void;

    auto enableProcessWidgets(
        HINALEA_IN bool enable
        ) -> void;

    auto onUpdateImage(
        ) -> void;

    auto onUpdateStatistics(
        HINALEA_IN int    min,
        HINALEA_IN int    max,
        HINALEA_IN int    saturation,
        HINALEA_IN double fps
        ) -> void;

    auto onDisplayTimerTimeout(
        ) -> void;

    auto onPowerButtonToggled(
        HINALEA_IN bool checked
        ) -> void;

    auto onRecordButtonToggled(
        HINALEA_IN bool checked
        ) -> void;

    auto onProcessButtonClicked(
        ) -> void;

    auto onCameraComboBoxCurrentIndexChanged(
        HINALEA_IN int index
        ) -> void;

    auto onHorizontalCheckBoxToggled(
        HINALEA_IN bool checked
        ) -> void;

    auto onVerticalCheckBoxToggled(
        HINALEA_IN bool checked
        ) -> void;

    auto onExposureSpinBoxValueChanged(
        HINALEA_IN int value
        ) -> void;

    auto onGainSpinBoxValueChanged(
        HINALEA_IN int value
        ) -> void;

    auto onGainModeSpinBoxValueChanged(
        HINALEA_IN int value
        ) -> void;

    auto onGapIndexSpinBoxValueChanged(
        HINALEA_IN int value
        ) -> void;

    auto onRefletanceSpinBoxValueChanged(
        HINALEA_IN double value
        ) -> void;

    auto onThresholdSpinBoxValueChanged(
        HINALEA_IN double value
        ) -> void;

    auto onLoadSettingsClicked(
        ) -> void;

    auto onLoadWhiteClicked(
        ) -> void;

    auto onLoadDarkClicked(
        ) -> void;

    auto onLoadMatrixClicked(
        ) -> void;

    auto onLoadGapClicked(
        ) -> void;

    auto onClearSettingsClicked(
        ) -> void;

    auto onClearWhiteClicked(
        ) -> void;

    auto onClearDarkClicked(
        ) -> void;

    auto onClearMatrixClicked(
        ) -> void;

    auto onClearGapClicked(
        ) -> void;

    auto onReflectanceCheckBoxToggled(
        HINALEA_IN bool checked
        ) -> void;

    auto onProgressChanged(
        HINALEA_IN int percent
        ) -> void;

    auto onThreadFailed(
        HINALEA_IN QString const & title,
        HINALEA_IN QString const & what
        ) -> void;

    auto classifyCallback(
        HINALEA_IN ::hinalea::DataCube const & data_cube,
        HINALEA_IN void const *                endmembers,
        HINALEA_IN ::hinalea::Int              observations
        ) -> void;

    ::hinalea::RealtimeClassifyCallback classifyCallback_{
        [ this ]( auto &&... args )
        {
            this->classifyCallback( HINALEA_FORWARD( args )... );
        }
        };

protected:
    virtual
    auto mousePressEvent(
        HINALEA_IN QMouseEvent * event
        ) -> void override;
};
