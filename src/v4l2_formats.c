/*******************************************************************************#
#           guvcview              http://guvcview.berlios.de                    #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "v4l2_formats.h"

#define SUP_PIX_FMT 6 /*number of supported formats*/

static SupFormats listSupFormats[] =
{
	{
		.format   = V4L2_PIX_FMT_MJPEG,
		.mode     = "mjpg",
		.hardware = 0
	},
	{
		.format   = V4L2_PIX_FMT_JPEG,
		.mode     ="jpeg",
		.hardware = 0
	},
	{
		.format   = V4L2_PIX_FMT_YUYV,
		.mode     = "yuyv",
		.hardware = 0
	},
	{
		.format   = V4L2_PIX_FMT_UYVY,
		.mode     = "uyvy",
		.hardware = 0
	},
	{
		.format   = V4L2_PIX_FMT_YYUV,
		.mode     = "yyuv",
		.hardware = 0
	},
	{
		.format   = V4L2_PIX_FMT_YUV420,
		.mode     = "yu12",
		.hardware = 0
	},
	{
		.format   = V4L2_PIX_FMT_SGBRG8,
		.mode     = "gbrg",
		.hardware = 0
	},
};

/* check if format is supported by guvcview
 * args:
 * pixfmt: V4L2 pixel format
 * return index from supported devices list 
 * or -1 if not supported                    */
int check_PixFormat(int pixfmt)
{
	int i=0;
	for (i=0; i<SUP_PIX_FMT; i++)
	{
		if (pixfmt == listSupFormats[i].format)
		{
			return (i); /*return index from supported formats list*/
		}
		else i++;
	}
	return (-1);
} 

/* set hardware flag for v4l2 pix format
 * args:
 * pixfmt: V4L2 pixel format
 * return index from supported devices list
 * or -1 if not supported                    */
int set_SupPixFormat(int pixfmt)
{
	int i=0;
	for (i=0; i<SUP_PIX_FMT; i++)
	{
		if (pixfmt == listSupFormats[i].format)
		{
			listSupFormats[i].hardware = 1; /*supported by hardware*/
			return (i);
		}
		else i++;
	}
	return(-1); /*not supported*/
}

/* check if format is supported by hardware
 * args:
 * pixfmt: V4L2 pixel format
 * return index from supported devices list
 * or -1 if not supported                    */
int check_SupPixFormat(int pixfmt)
{
	int i=0;
	for (i=0; i<SUP_PIX_FMT; i++)
	{
		if (pixfmt == listSupFormats[i].format)
		{
			if(listSupFormats[i].hardware > 0) return (i); /*supported by hardware*/
		}
		else i++;
	}
	return (-1);

}

/* convert v4l2 pix format to mode (Fourcc)
 * args:
 * pixfmt: V4L2 pixel format
 * mode: fourcc string (lower case)
 * returns 1 on success 
 * and -1 on failure (not supported)         */
int get_PixMode(int pixfmt, char *mode)
{
	int i=0;
	for (i=0; i<SUP_PIX_FMT; i++)
	{
		if (pixfmt == listSupFormats[i].format)
		{
			g_snprintf(mode, 5, "%s", listSupFormats[i].mode);
			return (1);
		}
		else i++;
	}
	return (-1);
}

/* converts mode (fourcc) to v4l2 pix format
 * args:
 * mode: fourcc string (lower case)
 * returns v4l2 pix format 
 * defaults to MJPG if mode not supported    */
int get_PixFormat(char *mode)
{
	int i=0;
	for (i=0; i<SUP_PIX_FMT; i++)
	{
		if (g_strcmp0(mode, listSupFormats[i].mode)==0)
		{
			return (listSupFormats[i].format);
		}
		else i++;
	}
	return (listSupFormats[0].format); /*default is- MJPG*/
}

/*clean video formats list
 * args: listVidFormats: array of VidFormats (list of video formats)
 * numb_formats: number of existing supported frame formats
 *
 * returns: void                                                       */
void freeFormats(VidFormats *listVidFormats, int numb_formats)
{
	int i=0;
	int j=0;
	for(i=0;i<numb_formats;i++)
	{
		for(j=0;j<listVidFormats[i].numb_res;j++)
		{
			g_free(listVidFormats[i].listVidCap[j].framerate_num);
			g_free(listVidFormats[i].listVidCap[j].framerate_denom);
		
		}
		g_free(listVidFormats[i].listVidCap);
	}
	g_free(listVidFormats);
}

/* enumerate frame intervals (fps)
 * args:
 * listVidFormats: array of VidFormats (list of video formats)
 * pixfmt: v4l2 pixel format that we want to list frame intervals for
 * width: video width that we want to list frame intervals for
 * height: video height that we want to list frame intervals for
 * fmtind: current index of format list
 * fsizeind: current index of frame size list
 * fd: device file descriptor
 * 
 * returns 0 if enumeration succeded or errno otherwise               */
static int enum_frame_intervals(VidFormats *listVidFormats, __u32 pixfmt, __u32 width, __u32 height, 
			int fmtind, int fsizeind, int fd)
{
	int ret=0;
	struct v4l2_frmivalenum fival;
	int list_fps=0;
	memset(&fival, 0, sizeof(fival));
	fival.index = 0;
	fival.pixel_format = pixfmt;
	fival.width = width;
	fival.height = height;
	listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_num=NULL;
	listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_denom=NULL;
	
	g_printf("\tTime interval between frame: ");
	while ((ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fival)) == 0) 
	{
		fival.index++;
		if (fival.type == V4L2_FRMIVAL_TYPE_DISCRETE) 
		{
			g_printf("%u/%u, ", fival.discrete.numerator, fival.discrete.denominator);
				
			list_fps++;
			listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_num = g_renew(
				int, listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_num, list_fps);
			listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_denom = g_renew(
				int, listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_denom, list_fps);
			
			listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_num[list_fps-1] = fival.discrete.numerator;
			listVidFormats[fmtind-1].listVidCap[fsizeind-1].framerate_denom[list_fps-1] = fival.discrete.denominator;
		} 
		else if (fival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) 
		{
			g_printf("{min { %u/%u } .. max { %u/%u } }, ",
				fival.stepwise.min.numerator, fival.stepwise.min.numerator,
				fival.stepwise.max.denominator, fival.stepwise.max.denominator);
			break;
		} 
		else if (fival.type == V4L2_FRMIVAL_TYPE_STEPWISE) 
		{
			g_printf("{min { %u/%u } .. max { %u/%u } / "
				"stepsize { %u/%u } }, ",
				fival.stepwise.min.numerator, fival.stepwise.min.denominator,
				fival.stepwise.max.numerator, fival.stepwise.max.denominator,
				fival.stepwise.step.numerator, fival.stepwise.step.denominator);
			break;
		}
	}
	
	listVidFormats[fmtind-1].listVidCap[fsizeind-1].numb_frates = list_fps;
	
	g_printf("\n");
	if (ret != 0 && errno != EINVAL) 
	{
		perror("VIDIOC_ENUM_FRAMEINTERVALS - Error enumerating frame intervals");
		return errno;
	}
	return 0;
}

/* enumerate frame sizes
 * args:
 * listVidFormats: array of VidFormats (list of video formats)
 * pixfmt: v4l2 pixel format that we want to list frame sizes for
 * fmtind: format list current index
 * width: pointer to integer containing the selected video width
 * height: pointer to integer containing the selected video height
 * fd: device file descriptor
 * 
 * returns 0 if enumeration succeded or errno otherwise               */
static int enum_frame_sizes(VidFormats *listVidFormats, __u32 pixfmt, int fmtind, int *width, int *height, int fd)
{
	int ret=0;
	int fsizeind=0; /*index for supported sizes*/
	listVidFormats[fmtind-1].listVidCap = NULL;
	struct v4l2_frmsizeenum fsize;

	memset(&fsize, 0, sizeof(fsize));
	fsize.index = 0;
	fsize.pixel_format = pixfmt;
	while ((ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsize)) == 0) 
	{
		fsize.index++;
		if (fsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) 
		{
			g_printf("{ discrete: width = %u, height = %u }\n",
				fsize.discrete.width, fsize.discrete.height);
			
			fsizeind++;
			listVidFormats[fmtind-1].listVidCap = g_renew(VidCap, 
				listVidFormats[fmtind-1].listVidCap,
				fsizeind);
			
			listVidFormats[fmtind-1].listVidCap[fsizeind-1].width = fsize.discrete.width;
			listVidFormats[fmtind-1].listVidCap[fsizeind-1].height = fsize.discrete.height;
			
			ret = enum_frame_intervals(listVidFormats, 
				pixfmt,
				fsize.discrete.width, 
				fsize.discrete.height,
				fmtind, 
				fsizeind, 
				fd);
			
			if (ret != 0) perror("  Unable to enumerate frame sizes");
		} 
		else if (fsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) 
		{
			g_printf("{ continuous: min { width = %u, height = %u } .. "
				"max { width = %u, height = %u } }\n",
				fsize.stepwise.min_width, fsize.stepwise.min_height,
				fsize.stepwise.max_width, fsize.stepwise.max_height);
			g_printf("  will not enumerate frame intervals.\n");
		} 
		else if (fsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) 
		{
			g_printf("{ stepwise: min { width = %u, height = %u } .. "
				"max { width = %u, height = %u } / "
				"stepsize { width = %u, height = %u } }\n",
				fsize.stepwise.min_width, fsize.stepwise.min_height,
				fsize.stepwise.max_width, fsize.stepwise.max_height,
				fsize.stepwise.step_width, fsize.stepwise.step_height);
			g_printf("  will not enumerate frame intervals.\n");
		} 
		else 
		{
			g_printerr("  fsize.type not supported: %d\n", fsize.type);
			g_printerr("     (Discrete: %d   Continuous: %d  Stepwise: %d)\n",
				V4L2_FRMSIZE_TYPE_DISCRETE,
				V4L2_FRMSIZE_TYPE_CONTINUOUS,
				V4L2_FRMSIZE_TYPE_STEPWISE);
		}
	}
	if (ret != 0 && errno != EINVAL) 
	{
		perror("VIDIOC_ENUM_FRAMESIZES - Error enumerating frame sizes");
		return errno;
	} 
	else if ((ret != 0) && (fsizeind == 0)) 
	{
		/* ------ gspca doesn't enumerate frame sizes ------ */
		/*       negotiate with VIDIOC_TRY_FMT instead       */
		
		fsizeind++;
		struct v4l2_format fmt;
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt.fmt.pix.width = *width;
		fmt.fmt.pix.height = *height;
		fmt.fmt.pix.pixelformat = pixfmt;
		fmt.fmt.pix.field = V4L2_FIELD_ANY;
		ret = ioctl(fd, VIDIOC_TRY_FMT, &fmt);
		/*use the returned values*/
		*width = fmt.fmt.pix.width;
		*height = fmt.fmt.pix.height;
		g_printf("{ ?GSPCA? : width = %u, height = %u }\n", *width, *height);
		
		if(listVidFormats[fmtind-1].listVidCap == NULL) 
		{
			listVidFormats[fmtind-1].listVidCap = g_renew( VidCap,
				listVidFormats[fmtind-1].listVidCap,
				fsizeind);
			listVidFormats[fmtind-1].listVidCap[0].framerate_num = g_renew( int,
				listVidFormats[fmtind-1].listVidCap[0].framerate_num,
				1);
			listVidFormats[fmtind-1].listVidCap[0].framerate_denom = g_renew( int,
				listVidFormats[fmtind-1].listVidCap[0].framerate_denom,
				1);
		} 
		else
		{
			g_printerr("assert failed: listVidCap not Null\n");
			return (-2);
		}
		listVidFormats[fmtind-1].listVidCap[0].width = *width;
		listVidFormats[fmtind-1].listVidCap[0].height = *height;
		listVidFormats[fmtind-1].listVidCap[0].framerate_num[0] = 1;
		listVidFormats[fmtind-1].listVidCap[0].framerate_denom[0] = 25;
	}
	
	listVidFormats[fmtind-1].numb_res=fsizeind;
	return 0;
}

/* enumerate frames (formats, sizes and fps)
 * args:
 * numb_formats: pointer to integer containing number of existing supported frame formats
 * width: current selected width
 * height: current selected height
 * fd: device file descriptor
 *
 * returns: pointer to VidFormats an allocated list of frame formats or NULL on failure */
VidFormats *enum_frame_formats(int *numb_formats, int *width, int *height, int fd)
{
	int ret=0;
	int fmtind=0;
	struct v4l2_fmtdesc fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.index = 0;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	VidFormats *listVidFormats = NULL;

	while ((ret = ioctl(fd, VIDIOC_ENUM_FMT, &fmt)) == 0) 
	{
		fmt.index++;
		g_printf("{ pixelformat = '%c%c%c%c', description = '%s' }\n",
				fmt.pixelformat & 0xFF, (fmt.pixelformat >> 8) & 0xFF,
				(fmt.pixelformat >> 16) & 0xFF, (fmt.pixelformat >> 24) & 0xFF,
				fmt.description);
		/*check if format is supported by guvcview, set hardware flag and allocate on device list*/
		if((ret=set_SupPixFormat(fmt.pixelformat)) >= 0) 
		{
			fmtind++;
			listVidFormats = g_renew(VidFormats, listVidFormats, fmtind);
			listVidFormats[fmtind-1].format=fmt.pixelformat;
			g_snprintf(listVidFormats[fmtind-1].fourcc,5,"%c%c%c%c",
				fmt.pixelformat & 0xFF, (fmt.pixelformat >> 8) & 0xFF,
				(fmt.pixelformat >> 16) & 0xFF, (fmt.pixelformat >> 24) & 0xFF);
			//enumerate frame sizes
			ret = enum_frame_sizes(listVidFormats, fmt.pixelformat, fmtind, width, height, fd);
			if (ret != 0)
				perror("  Unable to enumerate frame sizes.\n");
		}
		else
		{
			g_printerr("   { not supported - request format support at http://guvcview.berlios.de }\n");
		}
	}
	if (errno != EINVAL) {
		perror("VIDIOC_ENUM_FMT - Error enumerating frame formats");
	}
	*numb_formats=fmtind;
	return (listVidFormats);
}