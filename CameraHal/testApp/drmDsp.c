#include <stdio.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <drm_fourcc.h>
#include <string.h>

#include "./drmDsp/dev.h"
#include "./drmDsp/bo.h"
#include "./drmDsp/modeset.h"
#include "xf86drm.h"
#include "xf86drmMode.h"
#include "drmDsp.h"

struct drmDsp {
	struct fb_var_screeninfo vinfo;
	unsigned long screensize;
	char *fbp;
	struct sp_dev *dev;
	struct sp_plane **plane;
	struct sp_crtc *test_crtc;
	struct sp_plane *test_plane;
	int num_test_planes;
	struct sp_bo *bo[2];
	struct sp_bo *nextbo;
}gDrmDsp;

int initDrmDsp(int crtcId)
{
	int ret = 0,i = 0, j = 0;
	struct drmDsp *pDrmDsp = &gDrmDsp;

	memset(pDrmDsp,0,sizeof(struct drmDsp));

	pDrmDsp->dev = create_sp_dev();
	if (!pDrmDsp->dev) {
		printf("Failed to create sp_dev\n");
		return -1;
	}
#if 0
	ret = initialize_screens(pDrmDsp->dev);
	if (ret) {
		printf("Failed to initialize screens\n");
		return ret;
	}
#endif

	pDrmDsp->plane = calloc(pDrmDsp->dev->num_planes, sizeof(struct sp_plane *));
	if (!pDrmDsp->plane) {
		printf("Failed to allocate plane array\n");
		return -1;
	}

#if 1
	for (i = 0; i < pDrmDsp->dev->num_connectors; i++) {
		if (pDrmDsp->dev->connectors[i]->connection == DRM_MODE_CONNECTED) {
			/* it's connected, let's use this! */
			break;
		}
	}
	if (i == pDrmDsp->dev->num_connectors) {
		printf("no connected connector!\n");
		return -1;
	}
	/* find encoder: */
	for (j = 0; j < pDrmDsp->dev->num_encoders; j++) {
		if (pDrmDsp->dev->encoders[j]->encoder_id ==
					pDrmDsp->dev->connectors[i]->encoder_id) {
			break;
		}
	}
	if (j == pDrmDsp->dev->num_encoders) {
		printf("no encoder found!\n");
		return -1;
	}

	for (i = 0; i < pDrmDsp->dev->num_crtcs; i++) {
		if (pDrmDsp->dev->crtcs[i].crtc->crtc_id ==
					pDrmDsp->dev->encoders[j]->crtc_id) {
			break;
		}
	}
	if (i == pDrmDsp->dev->num_crtcs) {
		return -1;
	}

	pDrmDsp->test_crtc = &pDrmDsp->dev->crtcs[i];
	pDrmDsp->num_test_planes = pDrmDsp->test_crtc->num_planes;

	for (i = 0; i < pDrmDsp->test_crtc->num_planes; i++) {
		pDrmDsp->plane[i] = get_sp_plane(pDrmDsp->dev, pDrmDsp->test_crtc);
		if (is_supported_format(pDrmDsp->plane[i], DRM_FORMAT_NV12)) {
			pDrmDsp->test_plane = pDrmDsp->plane[i];
		}
	}

	if (!pDrmDsp->test_plane)
		return -1;

#else
	/* force vop big */
	pDrmDsp->test_crtc = &pDrmDsp->dev->crtcs[crtcId];
	pDrmDsp->num_test_planes = pDrmDsp->test_crtc->num_planes;
	for (i = 0; i < pDrmDsp->test_crtc->num_planes; i++) {
		pDrmDsp->plane[i] = get_sp_plane(pDrmDsp->dev, pDrmDsp->test_crtc);
		if (is_supported_format(pDrmDsp->plane[i], DRM_FORMAT_NV12))
			pDrmDsp->test_plane = pDrmDsp->plane[i];
	}

	if (!pDrmDsp->test_plane)
		return -1;
#endif
}

void deInitDrmDsp()
{
	struct drmDsp *pDrmDsp = &gDrmDsp;
	//if (pDrmDsp->bo[0])
	//	free_sp_bo(pDrmDsp->bo[0]);
	//if (pDrmDsp->bo[1])
        //        free_sp_bo(pDrmDsp->bo[1]);
	destroy_sp_dev(pDrmDsp->dev);
	memset(pDrmDsp,0,sizeof(struct drmDsp));
}
int drmDspFrame(int width,int height,int dmaFd,int fmt)
{
	struct drmDsp *pDrmDsp = &gDrmDsp;
	int wAlign16 = ((width + 15) & (~15));
	int hAlign16 = ((height + 15) & (~15));
	int frameSize = wAlign16 * hAlign16 * 3 / 2;
	int dmafd = dmaFd;
	uint32_t handles[4], pitches[4], offsets[4];
	uint32_t handle = 0;
	int fb_id, ret;

	if (DRM_FORMAT_NV12 != fmt)
		return -1;

	ret = drmPrimeFDToHandle(pDrmDsp->dev->fd, dmaFd, &handle);
	if (ret < 0) {
		printf("Could not get handle\n");
	}
	pitches[0] = wAlign16;
	offsets[0] = 0;
	handles[0] = handle;
	pitches[1] = wAlign16;
	offsets[1] = width*height;
	handles[1] = handle;

	ret = drmModeAddFB2(pDrmDsp->dev->fd, wAlign16, hAlign16,
			DRM_FORMAT_NV12, handles, pitches, offsets,
			&fb_id, 0);

	if (ret) {
		printf("%s:failed to create fb ret=%d\n", __func__,ret);
		printf("fd:%d ,wxh:%dx%d,format:%d,handles:%d,%d,pictches:%d,%d,offsets:%d,%d,fb_id:%d \n",
			pDrmDsp->dev->fd, wAlign16, hAlign16,DRM_FORMAT_NV12,
			handles[0],handles[1],pitches[0],pitches[1],
			offsets[0],offsets[1],fb_id);
		return ret;
	}

	ret = drmModeSetPlane(pDrmDsp->dev->ctrl_fd, pDrmDsp->test_plane->plane->plane_id,
		pDrmDsp->test_crtc->crtc->crtc_id, fb_id, 0, 0, 0,
		pDrmDsp->test_crtc->crtc->mode.hdisplay,
		pDrmDsp->test_crtc->crtc->mode.vdisplay,
		0, 0, width << 16, height << 16);

	if (ret) {
		printf("failed to set plane to crtc ret=%d\n", ret);
		return ret;
	}
}
