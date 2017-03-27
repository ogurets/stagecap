#define LOG_TAG "stagecap"

#include <stdio.h>
#include <signal.h>
#include <utils/Log.h>
#include <media/stagefright/CameraSource.h> 
#include <media/stagefright/MPEG4Writer.h>
#include <media/MediaPlayerInterface.h>

#include <binder/ProcessState.h>
#include <media/stagefright/AudioPlayer.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/OMXCodec.h>

#include <camera/ICamera.h>
#include <camera/Camera.h>
#include <camera/CameraParameters.h>
#include <camera/CameraHardwareInterface.h>

#include <media/MediaRecorderBase.h>
#include <ui/DisplayInfo.h>

#include <surfaceflinger/Surface.h>
#include <surfaceflinger/ISurface.h>
#include <surfaceflinger/SurfaceComposerClient.h>
#include <surfaceflinger/ISurfaceComposer.h>

#include <binder/IServiceManager.h>
#include <ui/Overlay.h>

enum CameraFlags {
    FLAGS_SET_CAMERA = 1L << 0,
    FLAGS_HOT_CAMERA = 1L << 1,
};


// http://www.xuebuyuan.com/1636118.html

using namespace android;

static const int32_t kFramerate = 24;  // fps
static const int32_t kIFramesIntervalSec = 1;
static const int32_t kVideoBitRate = 512 * 1024;
static const int32_t kAudioBitRate = 12200;
static const int64_t kDurationUs = 10000000LL;  // 10 seconds

#define AUDIO_CAP 0
#define DUMMY_SOURCE 0

#if DUMMY_SOURCE == 1
// TODO: implement
class HwCameraSource : public MediaSource {
private:
    int mWidth, mHeight;
    int mColorFormat;
    size_t mSize;
    int64_t mNumFramesOutput;
	sp<CameraHardwareInterface> mCamera;

    HwCameraSource(const HwCameraSource &);
    HwCameraSource &operator=(const HwCameraSource &);

public:
    HwCameraSource(int width, int height, int colorFormat)
        : mWidth(width),
          mHeight(height),
          mColorFormat(colorFormat),
          mSize((width * height * 3) / 2) {

		mCamera = HAL_openCameraHardware(int cameraId);

        // Check the color format to make sure
        // that the buffer size mSize it set correctly above.
        CHECK(colorFormat == OMX_COLOR_FormatYUV420SemiPlanar ||
              colorFormat == OMX_COLOR_FormatYUV420Planar);
    }

    virtual sp<MetaData> getFormat() {
        sp<MetaData> meta = new MetaData;
        meta->setInt32(kKeyWidth, mWidth);
        meta->setInt32(kKeyHeight, mHeight);
        meta->setInt32(kKeyColorFormat, mColorFormat);
        meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_RAW);

        return meta;
    }

    virtual status_t start(MetaData *params) {
        mNumFramesOutput = 0;
		mCamera->startRecording();
        return OK;
    }

    virtual status_t stop() {
		mCamera->stop();
        return OK;
    }

    virtual status_t read(
            MediaBuffer **buffer, const MediaSource::ReadOptions *options) {
        if (mNumFramesOutput == kFramerate * 10) {
            // Stop returning data after 10 secs.
            return ERROR_END_OF_STREAM;
        }

        // printf("DummySource::read\n");
        status_t err = mGroup.acquire_buffer(buffer);
        if (err != OK) {
            return err;
        }

        char x = (char)((double)rand() / RAND_MAX * 255);
        memset((*buffer)->data(), x, mSize);
        (*buffer)->set_range(0, mSize);
        (*buffer)->meta_data()->clear();
        (*buffer)->meta_data()->setInt64(
                kKeyTime, (mNumFramesOutput * 1000000) / kFramerate);
        ++mNumFramesOutput;

        // printf("DummySource::read - returning buffer\n");
        // LOGI("DummySource::read - returning buffer");
        return OK;
    }

protected:
    virtual ~HwCameraSource() {}
};


class DummySource : public MediaSource {

public:
    DummySource(int width, int height, int colorFormat)
        : mWidth(width),
          mHeight(height),
          mColorFormat(colorFormat),
          mSize((width * height * 3) / 2) {
        mGroup.add_buffer(new MediaBuffer(mSize));

        // Check the color format to make sure
        // that the buffer size mSize it set correctly above.
        CHECK(colorFormat == OMX_COLOR_FormatYUV420SemiPlanar ||
              colorFormat == OMX_COLOR_FormatYUV420Planar);
    }

    virtual sp<MetaData> getFormat() {
        sp<MetaData> meta = new MetaData;
        meta->setInt32(kKeyWidth, mWidth);
        meta->setInt32(kKeyHeight, mHeight);
        meta->setInt32(kKeyColorFormat, mColorFormat);
        meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_RAW);

        return meta;
    }

    virtual status_t start(MetaData *params) {
        mNumFramesOutput = 0;
        return OK;
    }

    virtual status_t stop() {
        return OK;
    }

    virtual status_t read(
            MediaBuffer **buffer, const MediaSource::ReadOptions *options) {
        if (mNumFramesOutput == kFramerate * 10) {
            // Stop returning data after 10 secs.
            return ERROR_END_OF_STREAM;
        }

        // printf("DummySource::read\n");
        status_t err = mGroup.acquire_buffer(buffer);
        if (err != OK) {
            return err;
        }

        char x = (char)((double)rand() / RAND_MAX * 255);
        memset((*buffer)->data(), x, mSize);
        (*buffer)->set_range(0, mSize);
        (*buffer)->meta_data()->clear();
        (*buffer)->meta_data()->setInt64(
                kKeyTime, (mNumFramesOutput * 1000000) / kFramerate);
        ++mNumFramesOutput;

        // printf("DummySource::read - returning buffer\n");
        // LOGI("DummySource::read - returning buffer");
        return OK;
    }

protected:
    virtual ~DummySource() {}

private:
    MediaBufferGroup mGroup;
    int mWidth, mHeight;
    int mColorFormat;
    size_t mSize;
    int64_t mNumFramesOutput;;

    DummySource(const DummySource &);
    DummySource &operator=(const DummySource &);
};

#endif

// Private clause circumvention
namespace android {
class Test {
public:
    static const sp<ISurface>& getISurface(const sp<Surface>& s) {
        return s->getISurface();
    }
};
};

    sp<Camera> mCamera;
    //sp<IMediaRecorderClient> mListener;
    //sp<MediaWriter> mWriter;

    video_source mVideoSource;
    output_format mOutputFormat;
    video_encoder mVideoEncoder;
    //bool mUse64BitFileOffset;
    int32_t mVideoWidth = 320;
	int32_t mVideoHeight = 240;
    int32_t mFrameRate = 30;
    //int32_t mVideoBitRate;
    //int32_t mAudioBitRate;
    //int32_t mAudioChannels;
    //int32_t mSampleRate;
    //int32_t mInterleaveDurationUs;
    //int32_t mIFramesIntervalSec;
    int32_t mCameraId = 0;
    //int32_t mVideoEncoderProfile;
    //int32_t mVideoEncoderLevel;
    //int32_t mMovieTimeScale;
    //int32_t mVideoTimeScale;
    //int32_t mAudioTimeScale;
    //int64_t mMaxFileSizeBytes;
    //int64_t mMaxFileDurationUs;
    //int64_t mTrackEveryTimeDurationUs;
    //int32_t mRotationDegrees;  // Clockwise 

	sp<SurfaceComposerClient> mComposerClient;
	sp<Surface> mPreviewSurface;

	sp<SurfaceControl> mSurfaceControl;
	sp<CameraSource> cameraSource;

	String8 mParams;
    int mOutputFd;
    int32_t mFlags = 0;

sp<MediaSource> createSource(const char *filename)
{
	sp<MediaSource> source;
    sp<MediaExtractor> extractor;

	if (strncasecmp(filename, "camera", 6)) {
		// Not camera
		LOGI("Starting dummy source");
		extractor = MediaExtractor::Create(new FileSource(filename));
		if (extractor == NULL) {
			return NULL;
		}

		size_t num_tracks = extractor->countTracks();

		sp<MetaData> meta;
		for (size_t i = 0; i < num_tracks; ++i) {
			meta = extractor->getTrackMetaData(i);
			CHECK(meta.get() != NULL);

			const char *mime;
			if (!meta->findCString(kKeyMIMEType, &mime)) {
				continue;
			}

			if (strncasecmp(mime, "video/", 6)) {
				continue;
			}

			source = extractor->getTrack(i);
			break;
		}

		return source;
	} else {
		// Camera
		LOGI("Starting camera");
        mCamera = Camera::connect(mCameraId);
        if (mCamera == 0) {
            LOGE("Camera connection could not be established.");
            return NULL;
        }
        mFlags &= ~FLAGS_HOT_CAMERA;
        mCamera->lock();
		LOGI("Camera lock success");

		CameraParameters params(mCamera->getParameters());
		params.setPreviewSize(mVideoWidth, mVideoHeight);
		params.setPreviewFrameRate(mFrameRate);
		LOGI("Params set");
		String8 s = params.flatten();
		if (OK != mCamera->setParameters(s)) {
			LOGE("Could not change settings."
				 " Someone else is using camera %d?", mCameraId);
			return NULL;
		}
		CameraParameters newCameraParams(mCamera->getParameters());

		LOGI("Param checkup started");
		// Check on video frame size
		int frameWidth = 0, frameHeight = 0;
		newCameraParams.getPreviewSize(&frameWidth, &frameHeight);
		if (frameWidth  < 0 || frameWidth  != mVideoWidth ||
			frameHeight < 0 || frameHeight != mVideoHeight) {
			LOGE("Failed to set the video frame size to %dx%d",
					mVideoWidth, mVideoHeight);
			return NULL;
		}

		// Check on video frame rate
		int frameRate = newCameraParams.getPreviewFrameRate();
		if (frameRate < 0 || (frameRate - mFrameRate) != 0) {
			LOGE("Failed to set frame rate to %d fps. The actual "
				 "frame rate is %d", mFrameRate, frameRate);
		}

		// This CHECK is good, since we just passed the lock/unlock
		// check earlier by calling mCamera->setParameters().

		// create pushbuffer surface
		// Method 1
		#if 0
			mComposerClient = new SurfaceComposerClient();
			mSurfaceControl = mComposerClient->createSurface(getpid(), 0, mVideoWidth, mVideoHeight, PIXEL_FORMAT_RGB_888 /*PIXEL_FORMAT_RGB_565*/, ISurfaceComposer::ePushBuffers); 

			mComposerClient->openTransaction();
			mSurfaceControl->setLayer(100000);
			mComposerClient->closeTransaction();

			mPreviewSurface = mSurfaceControl->getSurface();
		#endif

		// Method 2
		#if 0
			const String16 svname("SurfaceFlinger");
			const String8 surfname("hwsurf");
			sp<ISurfaceComposer> composer;
			getService(svname, &composer);
			mComposerClient = composer->createClientConnection();

			ISurfaceComposerClient::surface_data_t surfparams;
			mPreviewSurface = mComposerClient->createSurface(&surfparams, getpid(), surfname, 0, mVideoWidth, mVideoHeight, PIXEL_FORMAT_RGB_888 /*PIXEL_FORMAT_RGB_565*/, ISurfaceComposer::ePushBuffers);
		#endif

		// Method 3
		#if 1
			DisplayInfo dinfo;
			mComposerClient = new SurfaceComposerClient();
			status_t status = mComposerClient->getDisplayInfo(0, &dinfo);
			if (status) {
				LOGE("Cannot get display info");
				return NULL; 
			}
			mSurfaceControl = mComposerClient->createSurface(getpid(), 0, mVideoWidth, mVideoHeight, PIXEL_FORMAT_RGB_888, ISurfaceComposer::ePushBuffers);

			mComposerClient->openTransaction();
			mSurfaceControl->setLayer(0x40000000);
			mComposerClient->closeTransaction();

			mPreviewSurface = mSurfaceControl->getSurface(); 
		#endif

		if (!mPreviewSurface.get()) {
			LOGE("Surface was not created");
			return NULL;
		}

		/*sp<ISurface> isurface = Test::getISurface(mPreviewSurface);
		sp<OverlayRef> ref = isurface->createOverlay(mVideoWidth, mVideoHeight, OVERLAY_FORMAT_DEFAULT, 0); 
		if (!ref.get()) {
			LOGE("Overlay cannot be created");
			return NULL;
		}*/

		CHECK_EQ(OK, mCamera->setPreviewDisplay(mPreviewSurface)); // Needed even for recording
	
		LOGI("Constructing camerasource");
		cameraSource = CameraSource::CreateFromCamera(mCamera);
		return cameraSource;
	}
}

enum {
    kYUV420SP = 0,
    kYUV420P  = 1,
};

// returns -1 if mapping of the given color is unsuccessful
// returns an omx color enum value otherwise
static int translateColorToOmxEnumValue(int color) {
    switch (color) {
        case kYUV420SP:
            return OMX_COLOR_FormatYUV420SemiPlanar;
        case kYUV420P:
            return OMX_COLOR_FormatYUV420Planar;
        default:
            fprintf(stderr, "Unsupported color: %d\n", color);
            return -1;
    }
}

int g_runLoop = 1;

void sig_handler(int signum)
{
    g_runLoop = 0;
    printf("Stopping...");
}

int main(int argc, char **argv)
{
	signal(SIGINT, sig_handler);
    signal(SIGPIPE, sig_handler);
    android::ProcessState::self()->startThreadPool();

    DataSource::RegisterDefaultSniffers();

#if 1
    if (argc != 3) {
        fprintf(stderr, "usage: %s <filename> <input_color_format>\n", argv[0]);
        fprintf(stderr, "       <input_color_format>:  0 (YUV420SP) or 1 (YUV420P)\n");
        return 1;
    }

    int colorFormat = translateColorToOmxEnumValue(atoi(argv[2]));
    if (colorFormat == -1) {
        fprintf(stderr, "input color format must be 0 (YUV420SP) or 1 (YUV420P)\n");
        return 1;
    }
    OMXClient client;
    CHECK_EQ(client.connect(), OK);

    status_t err = OK;

#if DUMMY_SOURCE == 1
    int width = 720;
    int height = 480;
    sp<MediaSource> decoder = new DummySource(width, height, colorFormat);
#else
    sp<MediaSource> source = createSource(argv[1]);

    if (source == NULL) {
        fprintf(stderr, "Unable to find a suitable video track.\n");
        return 1;
    }

    LOGI("CameraSource created");
	sp<MetaData> meta = source->getFormat();

    //sp<MediaSource> decoder = OMXCodec::Create(client.interface(), meta, false /* createEncoder */, source);

    int width, height;
    bool success = meta->findInt32(kKeyWidth, &width);
    success = success && meta->findInt32(kKeyHeight, &height);
    CHECK(success);
#endif

    sp<MetaData> enc_meta = new MetaData;
    //enc_meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_H263);
    enc_meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_MPEG4);
    //enc_meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);
    enc_meta->setInt32(kKeyWidth, width);
    enc_meta->setInt32(kKeyHeight, height);
    enc_meta->setInt32(kKeySampleRate, kFramerate);
    enc_meta->setInt32(kKeyBitRate, kVideoBitRate);
    enc_meta->setInt32(kKeyStride, width);
    enc_meta->setInt32(kKeySliceHeight, height);
    enc_meta->setInt32(kKeyIFramesInterval, kIFramesIntervalSec);
    enc_meta->setInt32(kKeyColorFormat, colorFormat);

    //sp<MediaSource> encoder = OMXCodec::Create(client.interface(), enc_meta, true /* createEncoder */, decoder);
    LOGI("Creating encoder");
	sp<MediaSource> encoder = OMXCodec::Create(client.interface(), enc_meta, true /* createEncoder */, source);

#if 1
    LOGI("Creating writer");
	sp<MPEG4Writer> writer = new MPEG4Writer("/sdcard/output.mp4");
    writer->addSource(encoder);
    writer->setMaxFileDuration(kDurationUs);
    CHECK_EQ(OK, writer->start());
    while (!writer->reachedEOS() && g_runLoop) {
        fprintf(stderr, ".");
		printf(".");
        usleep(100000);
    }
    err = writer->stop();
#else
    CHECK_EQ(OK, encoder->start());

    MediaBuffer *buffer;
    while (encoder->read(&buffer) == OK) {
        printf(".");
        fflush(stdout);
        int32_t isSync;
        if (!buffer->meta_data()->findInt32(kKeyIsSyncFrame, &isSync)) {
            isSync = false;
        }

        printf("got an output frame of size %d%s\n", buffer->range_length(),
               isSync ? " (SYNC)" : "");

        buffer->release();
        buffer = NULL;
    }

    err = encoder->stop();
#endif

    printf("$\n");
    client.disconnect();
#endif

#if 0
    CameraSource *source = CameraSource::Create();
    source->start();

    printf("source = %p\n", source);

    for (int i = 0; i < 100; ++i) {
        MediaBuffer *buffer;
        status_t err = source->read(&buffer);
        CHECK_EQ(err, OK);

        printf("got a frame, data=%p, size=%d\n",
               buffer->data(), buffer->range_length());

        buffer->release();
        buffer = NULL;
    }

    err = source->stop();

    delete source;
    source = NULL;
#endif

    if (err != OK && err != ERROR_END_OF_STREAM) {
        fprintf(stderr, "record failed: %d\n", err);
        return 1;
    }
    return 0;
}

#if AUDIO_CAP == 1

int main(int argc, char **argv) {
    android::ProcessState::self()->startThreadPool();

    OMXClient client;
    CHECK_EQ(client.connect(), OK);

    const int32_t kSampleRate = 22050;
    const int32_t kNumChannels = 2;
    sp<MediaSource> audioSource = new SineSource(kSampleRate, kNumChannels);

#if 0
    sp<MediaPlayerBase::AudioSink> audioSink;
    AudioPlayer *player = new AudioPlayer(audioSink);
    player->setSource(audioSource);
    player->start();

    sleep(10);

    player->stop();
#endif

    sp<MetaData> encMeta = new MetaData;
    encMeta->setCString(kKeyMIMEType,
            1 ? MEDIA_MIMETYPE_AUDIO_AMR_WB : MEDIA_MIMETYPE_AUDIO_AAC);
    encMeta->setInt32(kKeySampleRate, kSampleRate);
    encMeta->setInt32(kKeyChannelCount, kNumChannels);
    encMeta->setInt32(kKeyMaxInputSize, 8192);
    encMeta->setInt32(kKeyBitRate, kAudioBitRate);

    sp<MediaSource> encoder =
        OMXCodec::Create(client.interface(), encMeta, true, audioSource);

    encoder->start();

    int32_t n = 0;
    status_t err;
    MediaBuffer *buffer;
    while ((err = encoder->read(&buffer)) == OK) {
        printf(".");
        fflush(stdout);

        buffer->release();
        buffer = NULL;

        if (++n == 100) {
            break;
        }
    }
    printf("$\n");

    encoder->stop();

    client.disconnect();

    return 0;
}
#endif 