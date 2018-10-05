//gcc lesson6.c -I/usr/include/ffmpeg -lavcodec -lavutil -lm -lavformat -lswscale -lGL -lglut -lpthread -o lesson6
#include <GL/glut.h>    // Header File For The GLUT Library 
#include <GL/gl.h>	// Header File For The OpenGL32 Library
#include <GL/glu.h>	// Header File For The GLu32 Library
#include <stdio.h>      // Header file for standard file i/o.
#include <stdlib.h>     // Header file for malloc/free.
#include <unistd.h>     // needed to sleep.
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <pthread.h>
static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static AVStream *video_stream = NULL;
static const char *src_filename = NULL;
static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;
static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int video_frame_count = 0;
static AVFrame *pFrameRGB=NULL;
static struct SwsContext *ctx = NULL;
static char *dstRGB = NULL;

/* ascii code for the escape key */
#define ESCAPE 27
const int TIMERMSECS = 40;

/* The number of our GLUT window */
int window; 

/* storage for one texture  */
int texture[1];

/* Image type - contains height, width, and data */
struct Image {
	unsigned long sizeX;
	unsigned long sizeY;
	char *data;
};
typedef struct Image Image;
Image *image1;

// quick and dirty bitmap loader...for 24 bit bitmaps with 1 plane only.  
// See http://www.dcs.ed.ac.uk/~mxr/gfx/2d/BMP.txt for more info.

static int decode_packet(int *got_frame, int cached)
{
        int ret = 0;
        if (pkt.stream_index == video_stream_idx) {
                /* decode video frame */
                ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
                if (ret < 0) {
                        fprintf(stderr, "Error decoding video frame\n");
                        return ret;
                }
                if (*got_frame) {
                        /*printf("video_frame%s n:%d coded_n:%d pts:%s\n",
                                        cached ? "(cached)" : "",
                                        video_frame_count++, frame->coded_picture_number,
                                        av_ts2timestr(frame->pts, &video_dec_ctx->time_base));*/
                        /* copy decoded frame to destination buffer:
 *                          *              * this is required since rawvideo expects non aligned data */
                        av_image_copy(video_dst_data, video_dst_linesize,
                                        (const uint8_t **)(frame->data), frame->linesize,
                                        video_dec_ctx->pix_fmt, video_dec_ctx->width, video_dec_ctx->height);
                        /* write to rawvideo file */
                        int numBytes = avpicture_get_size(PIX_FMT_RGB24, video_dec_ctx->width, video_dec_ctx->height);
                        uint8_t *buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

                        avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24, video_dec_ctx->width, video_dec_ctx->height);
                        ctx = sws_getContext(video_dec_ctx->width, video_dec_ctx->height,
                                        AV_PIX_FMT_YUV420P, video_dec_ctx->width, video_dec_ctx->height,
                                        AV_PIX_FMT_RGB24, SWS_BICUBIC, 0, 0, 0);

                        sws_scale(ctx, (const uint8_t **)frame->data, frame->linesize, 0, video_dec_ctx->height, pFrameRGB->data, pFrameRGB->linesize);

                        for(int y=0; y < video_dec_ctx->height; y++)
                                 memcpy(image1->data + video_dec_ctx->width*3*y, (pFrameRGB->data[0] + (y * pFrameRGB->linesize[0])), video_dec_ctx->width*3);

			sws_freeContext(ctx);
                        free(buffer);
                }
        }
        return ret;
}

static int open_codec_context(int *stream_idx,
                AVFormatContext *fmt_ctx, enum AVMediaType type)
{
        int ret;
        AVStream *st;
        AVCodecContext *dec_ctx = NULL;
        AVCodec *dec = NULL;
        ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
        if (ret < 0) {
                fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                                av_get_media_type_string(type), src_filename);
                return ret;
        } else {
                *stream_idx = ret;
                st = fmt_ctx->streams[*stream_idx];
                /* find decoder for the stream */
                dec_ctx = st->codec;
                dec = avcodec_find_decoder(dec_ctx->codec_id);
                if (!dec) {
                        fprintf(stderr, "Failed to find %s codec\n",
                                        av_get_media_type_string(type));
                        return ret;
                }
                if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
                        fprintf(stderr, "Failed to open %s codec\n",
                                        av_get_media_type_string(type));
                        return ret;
                }
        }
        return 0;
}

void* decodeThread (void *var)
{
        int ret = 0, got_frame;

        char *src_filename = "video.h264";
        /* register all formats and codecs */
        av_register_all();
        /* open input file, and allocate format context */
        if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
                fprintf(stderr, "Could not open source file %s\n", src_filename);
                exit(1);
        }
        /* retrieve stream information */
        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
                fprintf(stderr, "Could not find stream information\n");
                exit(1);
        }
        if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
                video_stream = fmt_ctx->streams[video_stream_idx];
                video_dec_ctx = video_stream->codec;
                /* allocate image where the decoded image will be put */
                ret = av_image_alloc(video_dst_data, video_dst_linesize,
                                video_dec_ctx->width, video_dec_ctx->height,
                                video_dec_ctx->pix_fmt, 1);
                if (ret < 0) {
                        fprintf(stderr, "Could not allocate raw video buffer\n");
                        goto end;
                }
                video_dst_bufsize = ret;
        }
        /* dump input information to stderr */
        av_dump_format(fmt_ctx, 0, src_filename, 0);
        if (!video_stream) {
                fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
                ret = 1;
                goto end;
        }
        frame = av_frame_alloc();//avcodec_alloc_frame();
        if (!frame) {
                fprintf(stderr, "Could not allocate frame\n");
                ret = AVERROR(ENOMEM);
                goto end;
        }
        pFrameRGB = av_frame_alloc();
	image1 = (Image *) malloc(sizeof(Image));
	if (image1 == NULL) {
		printf("Error allocating space for image");
		exit(0);
	}
        image1->data = (char*) malloc(video_dec_ctx->width*video_dec_ctx->height*3);
	image1->sizeX = video_dec_ctx->width;
	image1->sizeY = video_dec_ctx->height;

        /* initialize packet, set data to NULL, let the demuxer fill it */
        av_init_packet(&pkt);
        pkt.data = NULL;
        pkt.size = 0;
        /* read frames from the file */
        while (av_read_frame(fmt_ctx, &pkt) >= 0) {
                decode_packet(&got_frame, 0);
                av_free_packet(&pkt);
		usleep(40000);
        }
        /* flush cached frames */
        pkt.data = NULL;
	pkt.size = 0;
        do {
                decode_packet(&got_frame, 1);
        } while (got_frame);
        printf("Demuxing succeeded.\n");
end:
        if (video_dec_ctx)
                avcodec_close(video_dec_ctx);
        avformat_close_input(&fmt_ctx);
        av_free(frame);
        av_free(pFrameRGB);
        av_free(video_dst_data[0]);
	free(image1->data);
	image1->data = NULL;
	free(image1);
	image1 = NULL;
        free(dstRGB);
}

// Load Bitmaps And Convert To Textures
void LoadGLTextures() {	
	// Load Texture

	// allocate space for texture
	glClearColor (0.0, 0.0, 0.0, 0.0);
	glShadeModel(GL_FLAT);
	glEnable(GL_DEPTH_TEST);

	glEnable(GL_TEXTURE_2D);
	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	// Create Texture	
	glGenTextures(1, &texture[0]);
	glBindTexture(GL_TEXTURE_2D, texture[0]);   // 2d texture (x and y size)

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); // scale linearly when image smalled than texture
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST); // scale linearly when image bigger than texture
	glTexImage2D(GL_TEXTURE_2D, 0, 3, image1->sizeX, image1->sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image1->data);

}

/* A general OpenGL initialization function.  Sets all of the initial parameters. */
void InitGL(int Width, int Height)	        // We call this right after our OpenGL window is created.
{
	LoadGLTextures();				// Load The Texture(s) 
}

/* The function called when our window is resized (which shouldn't happen, because we're fullscreen) */
void ReSizeGLScene(int w, int h)
{
	glViewport(0, 0, w, h);		// Reset The Current Viewport And Perspective Transformation

	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	/*glFrustum (-1.0, 1.0, -1.0, 1.0, 10.0, 100.0);*/
	glOrtho (-1.0, 1.0, -1.0, 1.0, 10.0, 100.0);
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity (); 
	glTranslatef (0.0, 0.0, -10.0);
}

/* The main drawing function. */
void DrawGLScene()
{
	//InitGL(1920, 1080);
	//glClearColor (0.0, 0.0, 0.0, 0.0);
	//glShadeModel(GL_FLAT);
	//glEnable(GL_DEPTH_TEST);

	//glEnable(GL_TEXTURE_2D);
	//glPixelStorei (GL_UNPACK_ALIGNMENT, 1);

	if(image1 && image1->data)
	{
		glGenTextures(1, &texture[0]);
		glBindTexture(GL_TEXTURE_2D, texture[0]);   // 2d texture (x and y size)

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // scale linearly when image smalled than texture
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, 3, image1->sizeX, image1->sizeY, 0, GL_RGB, GL_UNSIGNED_BYTE, image1->data);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_TEXTURE_2D);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f,  0.0f);  // Top Left Of The Texture and Quad
		glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, -1.0f,  0.0f);  // Bottom Left Of The Texture and Quad
		glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f, -1.0f,  0.0f);  // Bottom Right Of The Texture and Quad
		glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,  1.0f,  0.0f);  // Top Right Of The Texture and Quad


		glEnd();

		glFlush();
		glDisable(GL_TEXTURE_2D);
		glDeleteTextures(1, &texture[0]);
		glutSwapBuffers();
	}

}

/* The function called whenever a key is pressed. */
void keyPressed(unsigned char key, int x, int y) 
{
	/* avoid thrashing this procedure */
	usleep(100);

	/* If escape is pressed, kill everything. */
	if (key == ESCAPE) 
	{ 
		/* shut down our window */
		glutDestroyWindow(window); 

		/* exit the program...normal termination. */
		exit(0);                   
	}
}

void animate(int t)
{
        //glutPostRedisplay();
	DrawGLScene();	
	glutTimerFunc(TIMERMSECS, animate, 0);
}

int main(int argc, char **argv) 
{  
	pthread_t t1;
	pthread_create(&t1, NULL, &decodeThread, NULL);


	glutInit(&argc, argv);  

	/* Select type of Display mode:   
	   Double buffer 
	   RGBA color
	   Alpha components supported 
	   Depth buffer */  
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);  

	/* get a 1920 x 1080 window */
	glutInitWindowSize(1920, 1080);  

	/* the window starts at the upper left corner of the screen */
	glutInitWindowPosition(0, 0);  

	/* Open a window */  
	window = glutCreateWindow(NULL);  
	sleep(1);

	/* Register the function to do all our OpenGL drawing. */
	glutDisplayFunc(&DrawGLScene);  

	/* Go fullscreen.  This is as soon as possible. */
	//glutFullScreen();

	/* Even if there are no events, redraw our gl scene. */
	//glutIdleFunc(&DrawGLScene);

	/* Register the function called when our window is resized. */
	//glutReshapeFunc(&ReSizeGLScene);

	/* Register the function called when the keyboard is pressed. */
	glutKeyboardFunc(&keyPressed);

	glutTimerFunc(TIMERMSECS, animate, 0);

	/* Initialize our window. */
	//InitGL(1920, 1080);

	/* Start Event Processing Engine */  
	glutMainLoop();  

	return 1;
}

