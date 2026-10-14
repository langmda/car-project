/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../mainwindow.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto MainWindow::qt_create_metaobjectdata<qt_meta_tag_ZN10MainWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MainWindow",
        "newFrameReceived",
        "",
        "imageData",
        "QJsonArray",
        "boxes",
        "timestamp",
        "requestStartCamera",
        "ip",
        "port",
        "requestStopCamera",
        "requestStartRecording",
        "path",
        "requestStopRecording",
        "on_CarControlButton_clicked",
        "on_SenSorDataButton_clicked",
        "on_MusicButton_clicked",
        "on_ParameterButton_clicked",
        "on_DataRecordButton_clicked",
        "on_CameraButton_clicked",
        "on_FaultButton_clicked",
        "on_TrajectoryButton_clicked",
        "on_VoiceInteractionButton_clicked",
        "on_Parameter_BackHomeButton_clicked",
        "on_CarControl_BackHomeButton_clicked",
        "on_SensorData_BackHomeButton_clicked",
        "on_Music_BackHomeButton_clicked",
        "on_DataRecord_BackHomeButton_clicked",
        "on_Camera_BackHomeButton_clicked",
        "on_Fault_BackHomeButton_clicked",
        "on_VoiceInteraction_BackHomeButton_clicked",
        "on_Trajectory_BackHomeButton_clicked",
        "on_FrontButton_clicked",
        "on_LeftButton_clicked",
        "on_RightButton_clicked",
        "on_BackButton_clicked",
        "on_SwitchModeButton_clicked",
        "on_LedButton_clicked",
        "on_RightCirlcleButton_clicked",
        "on_UpdatePWMBtn_clicked",
        "on_SwitchBtn_clicked",
        "on_PrevBtn_clicked",
        "on_NextBtn_clicked",
        "on_VolumeSlider_valueChanged",
        "value",
        "on_MusicListWidget_itemClicked",
        "QListWidgetItem*",
        "item",
        "on_ProgressSlider_sliderReleased",
        "on_MusicModeBtn_released",
        "on_UpdataParameterBtn_clicked",
        "on_DataRecordBtn_clicked",
        "on_DataClearBtn_clicked",
        "on_captureBtn_clicked",
        "on_recordBtn_clicked",
        "on_selectPhotoDirBtn_clicked",
        "on_selectVideoDirBtn_clicked",
        "on_cameraSwitchBtn_clicked",
        "onFrameReady",
        "QImage",
        "img",
        "onCameraConnected",
        "onCameraError",
        "msg",
        "onRecordingStarted",
        "onRecordingFailed",
        "on_Line_Follow_Button_clicked",
        "on_Trajectory_Z_Button_clicked",
        "on_Trajectory_J_Button_clicked",
        "on_Trajectory_U_Button_clicked",
        "on_VoiceInteractionSwitchBtn_clicked",
        "on_pushButton1_clicked",
        "on_pushButton2_clicked",
        "on_pushButton3_clicked"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'newFrameReceived'
        QtMocHelpers::SignalData<void(const QByteArray &, const QJsonArray &, double)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 3 }, { 0x80000000 | 4, 5 }, { QMetaType::Double, 6 },
        }}),
        // Signal 'requestStartCamera'
        QtMocHelpers::SignalData<void(const QString &, quint16)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { QMetaType::UShort, 9 },
        }}),
        // Signal 'requestStopCamera'
        QtMocHelpers::SignalData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'requestStartRecording'
        QtMocHelpers::SignalData<void(const QString &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 12 },
        }}),
        // Signal 'requestStopRecording'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'on_CarControlButton_clicked'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_SenSorDataButton_clicked'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_MusicButton_clicked'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_ParameterButton_clicked'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_DataRecordButton_clicked'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_CameraButton_clicked'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_FaultButton_clicked'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_TrajectoryButton_clicked'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_VoiceInteractionButton_clicked'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Parameter_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_CarControl_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_SensorData_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Music_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_DataRecord_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Camera_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Fault_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_VoiceInteraction_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Trajectory_BackHomeButton_clicked'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_FrontButton_clicked'
        QtMocHelpers::SlotData<void()>(32, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_LeftButton_clicked'
        QtMocHelpers::SlotData<void()>(33, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_RightButton_clicked'
        QtMocHelpers::SlotData<void()>(34, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_BackButton_clicked'
        QtMocHelpers::SlotData<void()>(35, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_SwitchModeButton_clicked'
        QtMocHelpers::SlotData<void()>(36, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_LedButton_clicked'
        QtMocHelpers::SlotData<void()>(37, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_RightCirlcleButton_clicked'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_UpdatePWMBtn_clicked'
        QtMocHelpers::SlotData<void()>(39, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_SwitchBtn_clicked'
        QtMocHelpers::SlotData<void()>(40, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_PrevBtn_clicked'
        QtMocHelpers::SlotData<void()>(41, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_NextBtn_clicked'
        QtMocHelpers::SlotData<void()>(42, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_VolumeSlider_valueChanged'
        QtMocHelpers::SlotData<void(int)>(43, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 44 },
        }}),
        // Slot 'on_MusicListWidget_itemClicked'
        QtMocHelpers::SlotData<void(QListWidgetItem *)>(45, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 46, 47 },
        }}),
        // Slot 'on_ProgressSlider_sliderReleased'
        QtMocHelpers::SlotData<void()>(48, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_MusicModeBtn_released'
        QtMocHelpers::SlotData<void()>(49, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_UpdataParameterBtn_clicked'
        QtMocHelpers::SlotData<void()>(50, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_DataRecordBtn_clicked'
        QtMocHelpers::SlotData<void()>(51, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_DataClearBtn_clicked'
        QtMocHelpers::SlotData<void()>(52, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_captureBtn_clicked'
        QtMocHelpers::SlotData<void()>(53, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_recordBtn_clicked'
        QtMocHelpers::SlotData<void()>(54, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_selectPhotoDirBtn_clicked'
        QtMocHelpers::SlotData<void()>(55, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_selectVideoDirBtn_clicked'
        QtMocHelpers::SlotData<void()>(56, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_cameraSwitchBtn_clicked'
        QtMocHelpers::SlotData<void()>(57, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFrameReady'
        QtMocHelpers::SlotData<void(const QImage &)>(58, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 59, 60 },
        }}),
        // Slot 'onCameraConnected'
        QtMocHelpers::SlotData<void()>(61, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onCameraError'
        QtMocHelpers::SlotData<void(const QString &)>(62, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 63 },
        }}),
        // Slot 'onRecordingStarted'
        QtMocHelpers::SlotData<void(const QString &)>(64, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 12 },
        }}),
        // Slot 'onRecordingFailed'
        QtMocHelpers::SlotData<void(const QString &)>(65, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 63 },
        }}),
        // Slot 'on_Line_Follow_Button_clicked'
        QtMocHelpers::SlotData<void()>(66, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Trajectory_Z_Button_clicked'
        QtMocHelpers::SlotData<void()>(67, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Trajectory_J_Button_clicked'
        QtMocHelpers::SlotData<void()>(68, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_Trajectory_U_Button_clicked'
        QtMocHelpers::SlotData<void()>(69, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_VoiceInteractionSwitchBtn_clicked'
        QtMocHelpers::SlotData<void()>(70, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_pushButton1_clicked'
        QtMocHelpers::SlotData<void()>(71, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_pushButton2_clicked'
        QtMocHelpers::SlotData<void()>(72, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'on_pushButton3_clicked'
        QtMocHelpers::SlotData<void()>(73, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MainWindow, qt_meta_tag_ZN10MainWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10MainWindowE_t>.metaTypes,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->newFrameReceived((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJsonArray>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3]))); break;
        case 1: _t->requestStartCamera((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint16>>(_a[2]))); break;
        case 2: _t->requestStopCamera(); break;
        case 3: _t->requestStartRecording((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->requestStopRecording(); break;
        case 5: _t->on_CarControlButton_clicked(); break;
        case 6: _t->on_SenSorDataButton_clicked(); break;
        case 7: _t->on_MusicButton_clicked(); break;
        case 8: _t->on_ParameterButton_clicked(); break;
        case 9: _t->on_DataRecordButton_clicked(); break;
        case 10: _t->on_CameraButton_clicked(); break;
        case 11: _t->on_FaultButton_clicked(); break;
        case 12: _t->on_TrajectoryButton_clicked(); break;
        case 13: _t->on_VoiceInteractionButton_clicked(); break;
        case 14: _t->on_Parameter_BackHomeButton_clicked(); break;
        case 15: _t->on_CarControl_BackHomeButton_clicked(); break;
        case 16: _t->on_SensorData_BackHomeButton_clicked(); break;
        case 17: _t->on_Music_BackHomeButton_clicked(); break;
        case 18: _t->on_DataRecord_BackHomeButton_clicked(); break;
        case 19: _t->on_Camera_BackHomeButton_clicked(); break;
        case 20: _t->on_Fault_BackHomeButton_clicked(); break;
        case 21: _t->on_VoiceInteraction_BackHomeButton_clicked(); break;
        case 22: _t->on_Trajectory_BackHomeButton_clicked(); break;
        case 23: _t->on_FrontButton_clicked(); break;
        case 24: _t->on_LeftButton_clicked(); break;
        case 25: _t->on_RightButton_clicked(); break;
        case 26: _t->on_BackButton_clicked(); break;
        case 27: _t->on_SwitchModeButton_clicked(); break;
        case 28: _t->on_LedButton_clicked(); break;
        case 29: _t->on_RightCirlcleButton_clicked(); break;
        case 30: _t->on_UpdatePWMBtn_clicked(); break;
        case 31: _t->on_SwitchBtn_clicked(); break;
        case 32: _t->on_PrevBtn_clicked(); break;
        case 33: _t->on_NextBtn_clicked(); break;
        case 34: _t->on_VolumeSlider_valueChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 35: _t->on_MusicListWidget_itemClicked((*reinterpret_cast<std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 36: _t->on_ProgressSlider_sliderReleased(); break;
        case 37: _t->on_MusicModeBtn_released(); break;
        case 38: _t->on_UpdataParameterBtn_clicked(); break;
        case 39: _t->on_DataRecordBtn_clicked(); break;
        case 40: _t->on_DataClearBtn_clicked(); break;
        case 41: _t->on_captureBtn_clicked(); break;
        case 42: _t->on_recordBtn_clicked(); break;
        case 43: _t->on_selectPhotoDirBtn_clicked(); break;
        case 44: _t->on_selectVideoDirBtn_clicked(); break;
        case 45: _t->on_cameraSwitchBtn_clicked(); break;
        case 46: _t->onFrameReady((*reinterpret_cast<std::add_pointer_t<QImage>>(_a[1]))); break;
        case 47: _t->onCameraConnected(); break;
        case 48: _t->onCameraError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 49: _t->onRecordingStarted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 50: _t->onRecordingFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 51: _t->on_Line_Follow_Button_clicked(); break;
        case 52: _t->on_Trajectory_Z_Button_clicked(); break;
        case 53: _t->on_Trajectory_J_Button_clicked(); break;
        case 54: _t->on_Trajectory_U_Button_clicked(); break;
        case 55: _t->on_VoiceInteractionSwitchBtn_clicked(); break;
        case 56: _t->on_pushButton1_clicked(); break;
        case 57: _t->on_pushButton2_clicked(); break;
        case 58: _t->on_pushButton3_clicked(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (MainWindow::*)(const QByteArray & , const QJsonArray & , double )>(_a, &MainWindow::newFrameReceived, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (MainWindow::*)(const QString & , quint16 )>(_a, &MainWindow::requestStartCamera, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (MainWindow::*)()>(_a, &MainWindow::requestStopCamera, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (MainWindow::*)(const QString & )>(_a, &MainWindow::requestStartRecording, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MainWindow::*)()>(_a, &MainWindow::requestStopRecording, 4))
            return;
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 59)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 59;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 59)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 59;
    }
    return _id;
}

// SIGNAL 0
void MainWindow::newFrameReceived(const QByteArray & _t1, const QJsonArray & _t2, double _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2, _t3);
}

// SIGNAL 1
void MainWindow::requestStartCamera(const QString & _t1, quint16 _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void MainWindow::requestStopCamera()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void MainWindow::requestStartRecording(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void MainWindow::requestStopRecording()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
