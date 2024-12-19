/* Webcamoid, webcam capture application.
 * Copyright (C) 2024  Gonzalo Exequiel Pedone
 *
 * Webcamoid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Webcamoid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include <QMutex>
#include <QQmlContext>
#include <QThread>
#include <QVariant>
#include <akfrac.h>
#include <akpacket.h>
#include <akvideocaps.h>
#include <akcompressedvideocaps.h>
#include <akpluginmanager.h>
#include <akvideoconverter.h>
#include <akvideopacket.h>
#include <akcompressedvideopacket.h>
#include <iak/akelement.h>
#include <wels/codec_api.h>

#include "videoencoderopenh264element.h"

/* Have tried adjusting several parameters, apply patches and many more things,
 * yet this codec does not seems to provide valid data.
 */

struct PixFormatTable
{
    AkVideoCaps::PixelFormat pixFormat;
    EVideoFormatType openh264Format;
    size_t depth;
    int flags;
    EProfileIdc profile;

    static inline const PixFormatTable *table()
    {
        static const PixFormatTable openh264PixFormatTable[] = {
            {AkVideoCaps::Format_yuv420p, videoFormatI420    , 8, 0, PRO_BASELINE},
            //{AkVideoCaps::Format_yvu420p, videoFormatYV12    , 8, 0, PRO_BASELINE},
            //{AkVideoCaps::Format_nv12   , videoFormatNV12    , 8, 0, PRO_BASELINE},
            //{AkVideoCaps::Format_yuyv422, videoFormatYUY2    , 8, 0, PRO_HIGH422 },
            //{AkVideoCaps::Format_yvyu422, videoFormatYVYU    , 8, 0, PRO_HIGH422 },
            //{AkVideoCaps::Format_uyvy422, videoFormatUYVY    , 8, 0, PRO_HIGH422 },
            //{AkVideoCaps::Format_bgr24  , videoFormatBGR     , 8, 0, PRO_HIGH444 },
            //{AkVideoCaps::Format_bgra   , videoFormatBGRA    , 8, 0, PRO_HIGH444 },
            //{AkVideoCaps::Format_rgba   , videoFormatRGBA    , 8, 0, PRO_HIGH444 },
            //{AkVideoCaps::Format_rgb24  , videoFormatRGB     , 8, 0, PRO_HIGH444 },
            //{AkVideoCaps::Format_abgr   , videoFormatABGR    , 8, 0, PRO_HIGH444 },
            //{AkVideoCaps::Format_argb   , videoFormatARGB    , 8, 0, PRO_HIGH444 },
            //{AkVideoCaps::Format_rgb555 , videoFormatRGB555  , 8, 0, PRO_HIGH444 },
            //{AkVideoCaps::Format_rgb565 , videoFormatRGB565  , 8, 0, PRO_HIGH444 },
            {AkVideoCaps::Format_none   , EVideoFormatType(0), 0, 0, PRO_UNKNOWN },
        };

        return openh264PixFormatTable;
    }

    static inline const PixFormatTable *byPixFormat(AkVideoCaps::PixelFormat format)
    {
        auto fmt = table();

        for (; fmt->pixFormat != AkVideoCaps::Format_none; fmt++)
            if (fmt->pixFormat == format)
                return fmt;

        return fmt;
    }
};

class VideoEncoderOpenH264ElementPrivate
{
    public:
        VideoEncoderOpenH264Element *self;
        AkVideoConverter m_videoConverter;
        AkCompressedVideoCaps m_outputCaps;
        VideoEncoderOpenH264Element::UsageType m_usageType {VideoEncoderOpenH264Element::UsageType_CameraVideoRealTime};
        VideoEncoderOpenH264Element::ComplexityMode m_complexityMode {VideoEncoderOpenH264Element::ComplexityMode_Low};
        VideoEncoderOpenH264Element::LogLevel m_logLevel {VideoEncoderOpenH264Element::LogLevel_Warning};
        bool m_globalHeader {true};
        bool m_enableFrameSkip {true};
        AkCompressedVideoPackets m_headers;
        ISVCEncoder *m_encoder {nullptr};
        SSourcePicture m_frame;
        QMutex m_mutex;
        qint64 m_id {0};
        int m_index {0};
        bool m_initialized {false};
        bool m_paused {false};
        qint64 m_dts {0};
        qint64 m_encodedTimePts {0};
        AkElementPtr m_fpsControl {akPluginManager->create<AkElement>("VideoFilter/FpsControl")};

        explicit VideoEncoderOpenH264ElementPrivate(VideoEncoderOpenH264Element *self);
        ~VideoEncoderOpenH264ElementPrivate();
        static const char *errorToString(int error);
        bool init();
        void uninit();
        void updateHeaders();
        void updateOutputCaps(const AkVideoCaps &inputCaps);
        void encodeFrame(const AkVideoPacket &src);
        void sendFrame(const QByteArray &packetData,
                       const SFrameBSInfo &info);
        ELevelIdc level(const AkVideoCaps &caps, EProfileIdc profile) const;
};

VideoEncoderOpenH264Element::VideoEncoderOpenH264Element():
    AkVideoEncoder()
{
    this->d = new VideoEncoderOpenH264ElementPrivate(this);
}

VideoEncoderOpenH264Element::~VideoEncoderOpenH264Element()
{
    this->d->uninit();
    delete this->d;
}

AkVideoEncoderCodecID VideoEncoderOpenH264Element::codec() const
{
    return AkCompressedVideoCaps::VideoCodecID_avc;
}

AkCompressedVideoCaps VideoEncoderOpenH264Element::outputCaps() const
{
    return this->d->m_outputCaps;
}

AkCompressedPackets VideoEncoderOpenH264Element::headers() const
{
    AkCompressedPackets packets;

    for (auto &header: this->d->m_headers)
        packets << header;

    return packets;
}

qint64 VideoEncoderOpenH264Element::encodedTimePts() const
{
    return this->d->m_encodedTimePts;
}

VideoEncoderOpenH264Element::UsageType VideoEncoderOpenH264Element::usageType() const
{
    return this->d->m_usageType;
}

VideoEncoderOpenH264Element::ComplexityMode VideoEncoderOpenH264Element::complexityMode() const
{
    return this->d->m_complexityMode;
}

VideoEncoderOpenH264Element::LogLevel VideoEncoderOpenH264Element::logLevel() const
{
    return this->d->m_logLevel;
}

bool VideoEncoderOpenH264Element::globalHeader() const
{
    return this->d->m_globalHeader;
}

bool VideoEncoderOpenH264Element::enableFrameSkip() const
{
    return this->d->m_enableFrameSkip;
}

QString VideoEncoderOpenH264Element::controlInterfaceProvide(const QString &controlId) const
{
    Q_UNUSED(controlId)

    return QString("qrc:/VideoEncoderOpenH264/share/qml/main.qml");
}

void VideoEncoderOpenH264Element::controlInterfaceConfigure(QQmlContext *context,
                                                       const QString &controlId) const
{
    Q_UNUSED(controlId)

    context->setContextProperty("VideoEncoderOpenH264", const_cast<QObject *>(qobject_cast<const QObject *>(this)));
    context->setContextProperty("controlId", this->objectName());
}

AkPacket VideoEncoderOpenH264Element::iVideoStream(const AkVideoPacket &packet)
{
    QMutexLocker mutexLocker(&this->d->m_mutex);

    if (this->d->m_paused || !this->d->m_initialized || !this->d->m_fpsControl)
        return {};

    bool discard = false;
    QMetaObject::invokeMethod(this->d->m_fpsControl.data(),
                              "discard",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, discard),
                              Q_ARG(AkVideoPacket, packet));

    if (discard)
        return {};

    this->d->m_videoConverter.begin();
    auto src = this->d->m_videoConverter.convert(packet);
    this->d->m_videoConverter.end();

    if (!src)
        return {};

    this->d->m_fpsControl->iStream(src);

    return {};
}

void VideoEncoderOpenH264Element::setUsageType(UsageType usageType)
{
    if (usageType == this->d->m_usageType)
        return;

    this->d->m_usageType = usageType;
    emit this->usageTypeChanged(usageType);
}

void VideoEncoderOpenH264Element::setComplexityMode(ComplexityMode complexityMode)
{
    if (complexityMode == this->d->m_complexityMode)
        return;

    this->d->m_complexityMode = complexityMode;
    emit this->complexityModeChanged(complexityMode);
}

void VideoEncoderOpenH264Element::setLogLevel(LogLevel logLevel)
{
    if (logLevel == this->d->m_logLevel)
        return;

    this->d->m_logLevel = logLevel;
    emit this->logLevelChanged(logLevel);
}

void VideoEncoderOpenH264Element::setGlobalHeader(bool globalHeader)
{
    if (globalHeader == this->d->m_globalHeader)
        return;

    this->d->m_globalHeader = globalHeader;
    emit this->globalHeaderChanged(globalHeader);
}

void VideoEncoderOpenH264Element::setEnableFrameSkip(bool enableFrameSkip)
{
    if (enableFrameSkip == this->d->m_enableFrameSkip)
        return;

    this->d->m_enableFrameSkip = enableFrameSkip;
    emit this->enableFrameSkipChanged(enableFrameSkip);
}

void VideoEncoderOpenH264Element::resetUsageType()
{
    this->setUsageType(UsageType_CameraVideoRealTime);
}

void VideoEncoderOpenH264Element::resetComplexityMode()
{
    this->setComplexityMode(ComplexityMode_Low);
}

void VideoEncoderOpenH264Element::resetLogLevel()
{
    this->setLogLevel(LogLevel_Warning);
}

void VideoEncoderOpenH264Element::resetGlobalHeader()
{
    this->setGlobalHeader(true);
}

void VideoEncoderOpenH264Element::resetEnableFrameSkip()
{
    this->setEnableFrameSkip(false);
}

void VideoEncoderOpenH264Element::resetOptions()
{
    AkVideoEncoder::resetOptions();
    this->resetUsageType();
}

bool VideoEncoderOpenH264Element::setState(ElementState state)
{
    auto curState = this->state();

    switch (curState) {
    case AkElement::ElementStateNull: {
        switch (state) {
        case AkElement::ElementStatePaused:
            this->d->m_paused = state == AkElement::ElementStatePaused;
        case AkElement::ElementStatePlaying:
            if (!this->d->init()) {
                this->d->m_paused = false;

                return false;
            }

            return AkElement::setState(state);
        default:
            break;
        }

        break;
    }
    case AkElement::ElementStatePaused: {
        switch (state) {
        case AkElement::ElementStateNull:
            this->d->uninit();

            return AkElement::setState(state);
        case AkElement::ElementStatePlaying:
            this->d->m_paused = false;

            return AkElement::setState(state);
        default:
            break;
        }

        break;
    }
    case AkElement::ElementStatePlaying: {
        switch (state) {
        case AkElement::ElementStateNull:
            this->d->uninit();

            return AkElement::setState(state);
        case AkElement::ElementStatePaused:
            this->d->m_paused = true;

            return AkElement::setState(state);
        default:
            break;
        }

        break;
    }
    }

    return false;
}

VideoEncoderOpenH264ElementPrivate::VideoEncoderOpenH264ElementPrivate(VideoEncoderOpenH264Element *self):
    self(self)
{
    this->m_videoConverter.setAspectRatioMode(AkVideoConverter::AspectRatioMode_Fit);

    QObject::connect(self,
                     &AkVideoEncoder::inputCapsChanged,
                     [this] (const AkVideoCaps &inputCaps) {
                         this->updateOutputCaps(inputCaps);
                     });

    if (this->m_fpsControl)
        QObject::connect(this->m_fpsControl.data(),
                         &AkElement::oStream,
                         [this] (const AkPacket &packet) {
                             this->encodeFrame(packet);
                         });
}

VideoEncoderOpenH264ElementPrivate::~VideoEncoderOpenH264ElementPrivate()
{

}

const char *VideoEncoderOpenH264ElementPrivate::errorToString(int error)
{
    static const struct ErrorCodesStr
    {
        CM_RETURN code;
        const char *str;
    } openh264EncErrorCodes[] = {
        {cmInitParaError  , "Invalid parameter"      },
        {cmUnknownReason  , "Unknown reason"         },
        {cmMallocMemeError, "Memory allocation error"},
        {cmInitExpected   , "Encoder not Initialized"},
        {cmUnsupportedData, "Unsupported data"       },
        {cmResultSuccess  , "Success"                },
    };

    auto ec = openh264EncErrorCodes;

    for (; ec->code != cmResultSuccess; ++ec)
        if (ec->code == error)
            return ec->str;

    if (ec->code == cmResultSuccess)
        return ec->str;

    static char openh264EncErrorStr[1024];
    snprintf(openh264EncErrorStr, 1024, "%d", error);

    return openh264EncErrorStr;
}

bool VideoEncoderOpenH264ElementPrivate::init()
{
    this->uninit();

    auto inputCaps = self->inputCaps();

    if (!inputCaps) {
        qCritical() << "Invalid input format.";

        return false;
    }

    auto eqFormat =
            PixFormatTable::byPixFormat(this->m_videoConverter.outputCaps().format());

    if (eqFormat->pixFormat == AkVideoCaps::Format_none)
        eqFormat = PixFormatTable::byPixFormat(AkVideoCaps::Format_yuv420p);

    auto result = WelsCreateSVCEncoder(&this->m_encoder);

    if (result != cmResultSuccess) {
        qCritical() << "Failed to create the encoder:" << errorToString(result);

        return false;
    }

    int32_t traceLevel = this->m_logLevel;
    result = this->m_encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &traceLevel);

    if (result != cmResultSuccess) {
        qCritical() << "Error setting the trace level:" << errorToString(result);
        WelsDestroySVCEncoder(this->m_encoder);
        this->m_encoder = nullptr;

        return false;
    }

    SEncParamExt param;
    result = this->m_encoder->GetDefaultParams(&param);

    if (result != cmResultSuccess) {
        qCritical() << "Error getting default parameters:" << errorToString(result);
        WelsDestroySVCEncoder(this->m_encoder);
        this->m_encoder = nullptr;

        return false;
    }

    param.iUsageType = EUsageType(this->m_usageType);
    param.iRCMode = RC_BITRATE_MODE;
    param.fMaxFrameRate = inputCaps.fps().value();
    param.iPicWidth = inputCaps.width();
    param.iPicHeight = inputCaps.height();
    param.iTargetBitrate = self->bitrate();
    param.uiIntraPeriod =
        qMax(self->gop() * inputCaps.fps().num()
             / (1000 * inputCaps.fps().den()), 1);
    param.iComplexityMode = ECOMPLEXITY_MODE(VideoEncoderOpenH264Element::ComplexityMode_Low);
    param.bEnableFrameSkip = this->m_enableFrameSkip;
    param.bEnableDenoise = 0;
    param.iMultipleThreadIdc = QThread::idealThreadCount();

    result = this->m_encoder->InitializeExt(&param);

    if (result != cmResultSuccess) {
        qCritical() << "Failed to initialize the encoder:" << errorToString(result);
        WelsDestroySVCEncoder(this->m_encoder);
        this->m_encoder = nullptr;

        return false;
    }

    int32_t videoFormat = eqFormat->openh264Format;
    result = this->m_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);

    if (result != cmResultSuccess) {
        qCritical() << "Error setting the data format:" << errorToString(result);
        WelsDestroySVCEncoder(this->m_encoder);
        this->m_encoder = nullptr;

        return false;
    }
/*
    SProfileInfo profile;
    profile.iLayer = 0;
    profile.uiProfileIdc = eqFormat->profile;
    result = this->m_encoder->SetOption(ENCODER_OPTION_PROFILE, &profile);

    if (result != cmResultSuccess) {
        qCritical() << "Error setting the profile:" << errorToString(result);
        WelsDestroySVCEncoder(this->m_encoder);
        this->m_encoder = nullptr;

        return false;
    }
*/
    memset(&this->m_frame, 0, sizeof(SSourcePicture));
    this->m_frame.iPicWidth = inputCaps.width();
    this->m_frame.iPicHeight = inputCaps.height();
    this->m_frame.iColorFormat = eqFormat->openh264Format;

    this->updateHeaders();

    if (this->m_fpsControl) {
        this->m_fpsControl->setProperty("fps", QVariant::fromValue(this->m_videoConverter.outputCaps().fps()));
        this->m_fpsControl->setProperty("fillGaps", self->fillGaps());
        QMetaObject::invokeMethod(this->m_fpsControl.data(),
                                  "restart",
                                  Qt::DirectConnection);
    }

    this->m_dts = 0;
    this->m_encodedTimePts = 0;
    this->m_initialized = true;

    return true;
}

void VideoEncoderOpenH264ElementPrivate::uninit()
{
    QMutexLocker mutexLocker(&this->m_mutex);

    if (!this->m_initialized)
        return;

    this->m_initialized = false;

    if (this->m_encoder) {
        this->m_encoder->Uninitialize();
        WelsDestroySVCEncoder(this->m_encoder);
        this->m_encoder = nullptr;
    }

    if (this->m_fpsControl)
        QMetaObject::invokeMethod(this->m_fpsControl.data(),
                                  "restart",
                                  Qt::DirectConnection);

    this->m_paused = false;
}

void VideoEncoderOpenH264ElementPrivate::updateHeaders()
{
    if (!this->m_globalHeader)
        return;

    SFrameBSInfo info;
    memset(&info, 0, sizeof(SFrameBSInfo));
    auto result = this->m_encoder->EncodeParameterSets(&info);

    if (result != cmResultSuccess) {
        qCritical() << "Failed reading the encoder parameters:" << errorToString(result);

        return;
    }

    QByteArray privateData;
    QDataStream ds(&privateData, QIODeviceBase::WriteOnly);
    ds << quint64(info.sLayerInfo[0].iNalCount);
    qsizetype offset = 0;

    for (int i = 0; i < info.sLayerInfo[0].iNalCount; i++) {
        auto size = info.sLayerInfo[0].pNalLengthInByte[i];
        ds << quint64(size);
        ds.writeRawData(reinterpret_cast<char *>(info.sLayerInfo[0].pBsBuf) + offset,
                        size);
        offset += size;
    }

    AkCompressedVideoPacket headerPacket(this->m_outputCaps,
                                         privateData.size());
    memcpy(headerPacket.data(),
           privateData.constData(),
           headerPacket.size());
    headerPacket.setTimeBase(this->m_outputCaps.rawCaps().fps().invert());
    headerPacket.setFlags(AkCompressedVideoPacket::VideoPacketTypeFlag_Header);
    this->m_headers = {headerPacket};
    emit self->headersChanged(self->headers());
}

void VideoEncoderOpenH264ElementPrivate::updateOutputCaps(const AkVideoCaps &inputCaps)
{
    if (!inputCaps) {
        if (!this->m_outputCaps)
            return;

        this->m_outputCaps = {};
        emit self->outputCapsChanged({});

        return;
    }

    auto eqFormat = PixFormatTable::byPixFormat(inputCaps.format());

    if (eqFormat->pixFormat == AkVideoCaps::Format_none)
        eqFormat = PixFormatTable::byPixFormat(AkVideoCaps::Format_yuv420p);

    auto fps = inputCaps.fps();

    if (!fps)
        fps = {30, 1};

    if (fps.value() > 30.0)
        fps = {30, 1};
    else if (fps.value() < 1.0)
        fps = {1, 1};

    this->m_videoConverter.setOutputCaps({eqFormat->pixFormat,
                                          inputCaps.width(),
                                          inputCaps.height(),
                                          fps});
    AkCompressedVideoCaps outputCaps(self->codec(),
                                     this->m_videoConverter.outputCaps(),
                                     self->bitrate());

    if (this->m_outputCaps == outputCaps)
        return;

    this->m_outputCaps = outputCaps;
    emit self->outputCapsChanged(outputCaps);
}

void VideoEncoderOpenH264ElementPrivate::encodeFrame(const AkVideoPacket &src)
{
    this->m_id = src.id();
    this->m_index = src.index();

    // Write the current frame.
    for (int plane = 0; plane < src.planes(); ++plane) {
        auto planeData = this->m_frame.pData[plane];
        auto oLineSize = this->m_frame.iStride[plane];
        auto lineSize = qMin<size_t>(src.lineSize(plane), oLineSize);
        auto heightDiv = src.heightDiv(plane);

        for (int y = 0; y < src.caps().height(); ++y) {
            auto ys = y >> heightDiv;
            memcpy(planeData + ys * oLineSize,
                   src.constLine(plane, y),
                   lineSize);
        }
    }

    this->m_frame.uiTimeStamp =
            qRound64(src.pts() * src.timeBase().value() * 1000);

    SFrameBSInfo info;
    memset(&info, 0, sizeof (SFrameBSInfo));
    auto result = this->m_encoder->EncodeFrame(&this->m_frame, &info);

    if (result != cmResultSuccess) {
        qCritical() << "Failed to encode frame:" << errorToString(result);

        return;
    }

    if (info.eFrameType == videoFrameTypeSkip)
        return;

    QByteArray packetData;
    int firstLayer = this->m_globalHeader? info.iLayerNum - 1: 0;

    for (int layer = firstLayer; layer < info.iLayerNum; ++layer) {
        auto layerInfo = info.sLayerInfo + layer;
        qsizetype layerSize = 0;

        for (int inal = 0; inal < layerInfo->iNalCount; inal++)
            layerSize += layerInfo->pNalLengthInByte[inal];

        packetData += QByteArray(reinterpret_cast<char *>(layerInfo->pBsBuf),
                                 layerSize);
    }

    if (packetData.isEmpty())
        return;

    this->sendFrame(packetData, info);

    this->m_encodedTimePts = src.pts() + src.duration();
    emit self->encodedTimePtsChanged(this->m_encodedTimePts);
}

void VideoEncoderOpenH264ElementPrivate::sendFrame(const QByteArray &packetData,
                                                   const SFrameBSInfo &info)
{
    AkCompressedVideoPacket packet(this->m_outputCaps, packetData.size());
    memcpy(packet.data(), packetData.constData(), packet.size());
    packet.setFlags(info.eFrameType == videoFrameTypeIDR?
                        AkCompressedVideoPacket::VideoPacketTypeFlag_KeyFrame:
                        AkCompressedVideoPacket::VideoPacketTypeFlag_None);
    packet.setPts(qRound64(info.uiTimeStamp
                           * this->m_outputCaps.rawCaps().fps().value()
                           / 1000.0));
    packet.setDts(this->m_dts);
    packet.setDuration(1);
    packet.setTimeBase(this->m_outputCaps.rawCaps().fps().invert());
    packet.setId(this->m_id);
    packet.setIndex(this->m_index);

    emit self->oStream(packet);
    this->m_dts++;
}

ELevelIdc VideoEncoderOpenH264ElementPrivate::level(const AkVideoCaps &caps,
                                                    EProfileIdc profile) const
{
    // https://blog.mediacoderhq.com/h264-profiles-and-levels/
    // https://en.wikipedia.org/wiki/Advanced_Video_Coding#Levels

    static const struct OpenH264LevelsDef
    {
        ELevelIdc level;
        quint64 mbps;
        quint64 frameSize;
        qint64 maxBitrate[4];
    } openH264Levels[] = {
        {LEVEL_1_0    , 1485   , 99L   , {64L    , 80L    , 192L   , 256L   }},
        {LEVEL_1_B    , 1485   , 99L   , {128L   , 160L   , 384L   , 512L   }},
        {LEVEL_1_1    , 3000   , 396L  , {192L   , 240L   , 576L   , 768L   }},
        {LEVEL_1_2    , 6000   , 396L  , {384L   , 480L   , 1152L  , 1536L  }},
        {LEVEL_1_3    , 11880  , 396L  , {768L   , 960L   , 2304L  , 3072L  }},
        {LEVEL_2_0    , 11880  , 396L  , {2000L  , 2500L  , 6000L  , 8000L  }},
        {LEVEL_2_1    , 19800  , 792L  , {4000L  , 5000L  , 12000L , 16000L }},
        {LEVEL_2_2    , 20250  , 1620L , {4000L  , 5000L  , 12000L , 16000L }},
        {LEVEL_3_0    , 40500  , 1620L , {10000L , 12500L , 30000L , 40000L }},
        {LEVEL_3_1    , 108000 , 3600L , {14000L , 17500L , 42000L , 56000L }},
        {LEVEL_3_2    , 216000 , 5120L , {20000L , 25000L , 60000L , 80000L }},
        {LEVEL_4_0    , 245760 , 8192L , {20000L , 25000L , 60000L , 80000L }},
        {LEVEL_4_1    , 245760 , 8192L , {50000L , 50000L , 150000L, 200000L}},
        {LEVEL_4_2    , 522240 , 8704L , {50000L , 50000L , 150000L, 200000L}},
        {LEVEL_5_0    , 589824 , 22080L, {135000L, 168750L, 405000L, 540000L}},
        {LEVEL_5_1    , 983040 , 36864L, {240000L, 300000L, 720000L, 960000L}},
        {LEVEL_5_2    , 2073600, 36864L, {240000L, 300000L, 720000L, 960000L}},
        {LEVEL_UNKNOWN, 0      , 0L    , {0L     , 0L     , 0L     , 0L     }},
    };

    int mbWidth = (caps.width() + 15) / 16;
    int mbHeight = (caps.height() + 15) / 16;
    quint64 lumaPictureSize = mbWidth * mbHeight;
    quint64 lumaSampleRate = qRound64(lumaPictureSize * caps.fps().value());
    int bitrate = self->bitrate();
    int bitrateIndex = 0;

    switch (profile) {
    case PRO_BASELINE:
    case PRO_MAIN:
    case PRO_EXTENDED:
        bitrateIndex = 0;

        break;

    case PRO_HIGH:
        bitrateIndex = 1;

        break;

    case PRO_HIGH10:
        bitrateIndex = 2;

        break;
    case PRO_HIGH422:
    case PRO_HIGH444:
        bitrateIndex = 3;

        break;

    default:
        break;
    }

    for (auto level = openH264Levels; level->level != LEVEL_UNKNOWN; ++level)
        if (level->frameSize >= lumaPictureSize
            && level->mbps >= lumaSampleRate
            && 1000 * level->maxBitrate[bitrateIndex] >= bitrate) {
            return level->level;
        }

    return LEVEL_UNKNOWN;
}

#include "moc_videoencoderopenh264element.cpp"
