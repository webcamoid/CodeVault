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

#ifndef VIDEOENCODEROPENH264ELEMENT_H
#define VIDEOENCODEROPENH264ELEMENT_H

#include <iak/akvideoencoder.h>

class VideoEncoderOpenH264ElementPrivate;

class VideoEncoderOpenH264Element: public AkVideoEncoder
{
    Q_OBJECT
    Q_PROPERTY(UsageType usageType
               READ usageType
               WRITE setUsageType
               RESET resetUsageType
               NOTIFY usageTypeChanged)
    Q_PROPERTY(ComplexityMode complexityMode
               READ complexityMode
               WRITE setComplexityMode
               RESET resetComplexityMode
               NOTIFY complexityModeChanged)
    Q_PROPERTY(LogLevel logLevel
               READ logLevel
               WRITE setLogLevel
               RESET resetLogLevel
               NOTIFY logLevelChanged)
    Q_PROPERTY(bool globalHeader
               READ globalHeader
               WRITE setGlobalHeader
               RESET resetGlobalHeader
               NOTIFY globalHeaderChanged)
    Q_PROPERTY(bool enableFrameSkip
               READ enableFrameSkip
               WRITE setEnableFrameSkip
               RESET resetEnableFrameSkip
               NOTIFY enableFrameSkipChanged)

    public:
        enum UsageType
        {
            UsageType_Unknown = -1,
            UsageType_CameraVideoRealTime,
            UsageType_ScreenContentRealTime,
            UsageType_CameraVideoNonRealTime,
            UsageType_ScreenContentNonRealTime,
            UsageType_InputContentTypeAll,
        };
        Q_ENUM(UsageType)

        enum ComplexityMode
        {
          ComplexityMode_Low,
          ComplexityMode_Medium,
          ComplexityMode_High,
        };
        Q_ENUM(ComplexityMode)

        enum LogLevel
        {
            LogLevel_Quiet   = 0x0,
            LogLevel_Error   = 1 << 0,
            LogLevel_Warning = 1 << 1,
            LogLevel_Info    = 1 << 2,
            LogLevel_Debug   = 1 << 3,
            LogLevel_Detail  = 1 << 4,
            LogLevel_Resv    = 1 << 5,
        };
        Q_ENUM(LogLevel)

        VideoEncoderOpenH264Element();
        ~VideoEncoderOpenH264Element();

        Q_INVOKABLE AkVideoEncoderCodecID codec() const override;
        Q_INVOKABLE AkCompressedVideoCaps outputCaps() const override;
        Q_INVOKABLE AkCompressedPackets headers() const override;
        Q_INVOKABLE qint64 encodedTimePts() const override;
        Q_INVOKABLE UsageType usageType() const;
        Q_INVOKABLE ComplexityMode complexityMode() const;
        Q_INVOKABLE LogLevel logLevel() const;
        Q_INVOKABLE bool globalHeader() const;
        Q_INVOKABLE bool enableFrameSkip() const;

    private:
        VideoEncoderOpenH264ElementPrivate *d;

    protected:
        QString controlInterfaceProvide(const QString &controlId) const override;
        void controlInterfaceConfigure(QQmlContext *context,
                                       const QString &controlId) const override;
        AkPacket iVideoStream(const AkVideoPacket &packet) override;

    signals:
        void usageTypeChanged(UsageType usageType);
        void complexityModeChanged(ComplexityMode complexityMode);
        void logLevelChanged(LogLevel logLevel);
        void globalHeaderChanged(bool globalHeader);
        void enableFrameSkipChanged(bool enableFrameSkip);

    public slots:
        void setUsageType(UsageType usageType);
        void setComplexityMode(ComplexityMode complexityMode);
        void setLogLevel(LogLevel logLevel);
        void setGlobalHeader(bool globalHeader);
        void setEnableFrameSkip(bool enableFrameSkip);
        void resetUsageType();
        void resetComplexityMode();
        void resetLogLevel();
        void resetGlobalHeader();
        void resetEnableFrameSkip();
        void resetOptions() override;
        bool setState(AkElement::ElementState state) override;
};

Q_DECLARE_METATYPE(VideoEncoderOpenH264Element::UsageType)
Q_DECLARE_METATYPE(VideoEncoderOpenH264Element::ComplexityMode)
Q_DECLARE_METATYPE(VideoEncoderOpenH264Element::LogLevel)

#endif // VIDEOENCODEROPENH264ELEMENT_H
