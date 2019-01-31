#include <assert.h>
#include <stdlib.h>
#include <3ds.h>

#include "C2D_helper.h"
#include "common.h"
#include "config.h"
#include "fs.h"
#include "libnsgif.h"
#include "touch.h"
#include "utils.h"

enum IMAGE_STATES {
	DIMENSION_DEFAULT = 0,
	DIMENSION_NINTENDO_SCREENSHOT = 1,
	DIMENSION_3DSHELL_SCREENSHOT = 2
};

static char album[512][512];
static int count = 0, selection = 0, dimensions = 0, pos_x = 0, pos_y = 0;
static float width = 0, height = 0;
static float scale_factor = 0.0f;
static C2D_Image image;

static void Gallery_FreeImage(C2D_Image *image) {
	C3D_TexDelete(image->tex);
	linearFree((Tex3DS_SubTexture *)image->subtex);
	C2D_TargetClear(RENDER_TOP, C2D_Color32(33, 39, 43, 255));
	C2D_TargetClear(RENDER_BOTTOM, C2D_Color32(33, 39, 43, 255));
}

static bool Gallery_DrawImage(C2D_Image image, float x, float y, float start, float end, float w, float h, float zoom_factor) {
	C2D_DrawParams params = {
		{ x - (pos_x * zoom_factor - pos_x) / 2, y - (pos_y * zoom_factor - pos_y) / 2, w * zoom_factor, h * zoom_factor },
		{ start, end },
		0.5f, 0.0f
	};

	return C2D_DrawImage(image, &params, NULL);
}

static Result Gallery_GetImageList(void) {
	Handle dir;
	Result ret = 0;

	if (R_SUCCEEDED(ret = FSUSER_OpenDirectory(&dir, archive, fsMakePath(PATH_ASCII, cwd)))) {
		u32 entryCount = 0;
		FS_DirectoryEntry *entries = (FS_DirectoryEntry *)calloc(MAX_FILES, sizeof(FS_DirectoryEntry));

		if (R_SUCCEEDED(ret = FSDIR_Read(dir, &entryCount, MAX_FILES, entries))) {
			qsort(entries, entryCount, sizeof(FS_DirectoryEntry), Utils_Alphasort);
			char name[256] = {'\0'};

			for (u32 i = 0; i < entryCount; i++) {
				Utils_U16_To_U8((u8 *)&name[0], entries[i].name, 255);

				if ((!strncasecmp(entries[i].shortExt, "png", 3)) || (!strncasecmp(entries[i].shortExt, "jpg", 3)) || (!strncasecmp(entries[i].shortExt, "bmp", 3))
					|| (!strncasecmp(entries[i].shortExt, "gif", 3))) {
					strcpy(album[count], cwd);
					strcpy(album[count] + strlen(album[count]), name);
					count++;
				}
			}
		}
		else {
			free(entries);
			return ret;
		}

		free(entries);

		if (R_FAILED(ret = FSDIR_Close(dir))) // Close directory
			return ret;
	}
	else
		return ret;

	return 0;
}

static int Gallery_GetCurrentIndex(char *path) {
	for(int i = 0; i < count; ++i) {
		if (!strcmp(album[i], path))
			return i;
	}

	return 0;
}

static void Gallery_LoadTexture(char *path) {
	selection = Gallery_GetCurrentIndex(path);

	char extension[5] = {0};
	strncpy(extension, &path[strlen(path) - 4], 4);

	if (!strncasecmp(extension, ".bmp", 4))
		Draw_LoadImageBMPFile(&image, path);
	else if (!strncasecmp(extension, ".jpg", 4) || !strncasecmp(extension, ".jpeg", 4))
		Draw_LoadImageJPGFile(&image, path);
	else if (!strncasecmp(extension, ".png", 4))
		Draw_LoadImagePNGFile(&image, path);
	else if (!strncasecmp(extension, ".gif", 4))
		Draw_LoadImageGIFFile(&image, path);

	if ((image.subtex->width == 432) && (image.subtex->height == 528)) // Nintnedo's screenshot (both screens) dimensions.
		dimensions = DIMENSION_NINTENDO_SCREENSHOT;
	else if ((image.subtex->width == 400) && ((image.subtex->height == 480) || (image.subtex->height == 482)))
		dimensions = DIMENSION_3DSHELL_SCREENSHOT;
	else {
		dimensions = DIMENSION_DEFAULT;

		if ((float)image.subtex->height > 240.0f) {
			scale_factor = (240.0f / (float)image.subtex->height);
			width = (float)image.subtex->width * scale_factor;
			height = (float)image.subtex->height * scale_factor;
		}
		else {
			width = (float)image.subtex->width;
			height = (float)image.subtex->height;
		}
	}
}

static void Gallery_HandleNext(bool forward) {
	if (forward)
		selection++;
	else
		selection--;

	Utils_SetMax(&selection, 0, (count - 1));
	Utils_SetMin(&selection, (count - 1), 0);

	Gallery_FreeImage(&image);
	Gallery_LoadTexture(album[selection]);
}

static void *gif_bitmap_create(int width, int height) {
	return calloc(width * height, 4);
}

static void gif_bitmap_set_opaque(void *bitmap, bool opaque) {
	(void) opaque;  /* unused */
	assert(bitmap);
}

static bool gif_bitmap_test_opaque(void *bitmap) {
	assert(bitmap);
	return false;
}

static u8 *gif_bitmap_get_buffer(void *bitmap) {
	assert(bitmap);
	return bitmap;
}

static void gif_bitmap_destroy(void *bitmap) {
	assert(bitmap);
	free(bitmap);
}

static void gif_bitmap_modified(void *bitmap) {
	assert(bitmap);
	return;
}
bool Gallery_DrawImage(C2D_Image image, float x, float y, float start, float end, float w, float h, float zoom_factor)

bool Gallery_DisplayGif(char *path, bool (*function)(C2D_Image, float, float, float, float, float, float, float)) {
	bool gif_is_animated = false;

	gif_bitmap_callback_vt gif_bitmap_callbacks = {
		gif_bitmap_create,
		gif_bitmap_destroy,
		gif_bitmap_get_buffer,
		gif_bitmap_set_opaque,
		gif_bitmap_test_opaque,
		gif_bitmap_modified
	};

	gif_animation gif;
	u32 size = 0;
	gif_result code = GIF_OK;

	gif_create(&gif, &gif_bitmap_callbacks);
	u8 *data = Draw_LoadExternalImageFile(path, &size);

	do {
		code = gif_initialise(&gif, size, data);
		if (code != GIF_OK && code != GIF_WORKING) {
			linearFree(data);
			return false;
		}
	} while (code != GIF_OK);

	if ((gif.width > 4096) || (gif.height > 4096)) {
		linearFree(data);
		return false;
	}

	u64 max_size = (u64)gif.width * (u64)gif.height * 4LLU * (u64)gif.frame_count;
	if ((gif.width * gif.height * 4 * gif.frame_count) != max_size) {
		linearFree(data);
		return false;
	}

	gif_is_animated = gif.frame_count > 1;

	C2D_Image gif_image[gif.frame_count];
	u64 frame_times[gif.frame_count];
	float frame_width[gif.frame_count], frame_height[gif.frame_count];
	float scale_factor = 0.0f;

	if (gif_is_animated) {
		for (unsigned int i = 0; i < gif.frame_count; i++) {
			code = gif_decode_frame(&gif, i);
			if (code != GIF_OK)
				break;

			frame_times[i] = (u64)gif.frames[i].frame_delay * 10000000;
			if (!frame_times[i])
				frame_times[i] = 100000000;

			u8 *image = (u8 *)gif.frame_image;

			for (u32 row = 0; row < (u32)gif.width; row++) {
				for (u32 col = 0; col < (u32)gif.height; col++) {
					u32 z = (row + col * (u32)gif.width) * 4;

					u8 r = *(u8 *)(image + z);
					u8 g = *(u8 *)(image + z + 1);
					u8 b = *(u8 *)(image + z + 2);
					u8 a = *(u8 *)(image + z + 3);

					*(image + z) = a;
					*(image + z + 1) = b;
					*(image + z + 2) = g;
					*(image + z + 3) = r;
				}
			}

			C3D_Tex *tex = linearAlloc(sizeof(C3D_Tex));
			Tex3DS_SubTexture *subtex = linearAlloc(sizeof(Tex3DS_SubTexture));
			Draw_C3DTexToC2DImage(tex, subtex, image, (u32)(gif.width * gif.height * 4), (u32)gif.width, (u32)gif.height, GPU_RGBA8);
			gif_image[i].tex = tex;
			gif_image[i].subtex = subtex;

			if ((float)gif_image[i].subtex->height > 240.0f) {
				scale_factor = (240.0f / (float)gif_image[i].subtex->height);
				frame_width[i] = (float)gif_image[i].subtex->width * scale_factor;
				frame_height[i] = (float)gif_image[i].subtex->height * scale_factor;
				scale_factor = 0;
			}
			else {
				frame_width[i] = (float)gif_image[i].subtex->width;
				frame_height[i] = (float)gif_image[i].subtex->height;
			}
		}
	}
	else {
		code = gif_decode_frame(&gif, 0);
		if (code != GIF_OK)
			return false;

		frame_times[0] = (u64)gif.frames[0].frame_delay * 10000000;
		if (!frame_times[0])
			frame_times[0] = 100000000;

		u8 *image = (u8 *)gif.frame_image;

		for (u32 row = 0; row < (u32)gif.width; row++) {
			for (u32 col = 0; col < (u32)gif.height; col++) {
				u32 z = (row + col * (u32)gif.width) * 4;

				u8 r = *(u8 *)(image + z);
				u8 g = *(u8 *)(image + z + 1);
				u8 b = *(u8 *)(image + z + 2);
				u8 a = *(u8 *)(image + z + 3);

				*(image + z) = a;
				*(image + z + 1) = b;
				*(image + z + 2) = g;
				*(image + z + 3) = r;
			}
		}

		C3D_Tex *tex = linearAlloc(sizeof(C3D_Tex));
		Tex3DS_SubTexture *subtex = linearAlloc(sizeof(Tex3DS_SubTexture));
		Draw_C3DTexToC2DImage(tex, subtex, image, (u32)(gif.width * gif.height * 4), (u32)gif.width, (u32)gif.height, GPU_RGBA8);
		gif_image[0].tex = tex;
		gif_image[0].subtex = subtex;

		if ((float)gif_image[0].subtex->height > 240.0f) {
			scale_factor = (240.0f / (float)gif_image[0].subtex->height);
			frame_width[0] = (float)gif_image[0].subtex->width * scale_factor;
			frame_height[0] = (float)gif_image[0].subtex->height * scale_factor;
			scale_factor = 0;
		}
		else {
			frame_width[0] = (float)gif_image[0].subtex->width;
			frame_height[0] = (float)gif_image[0].subtex->height;
		}
	}

	unsigned int count = 0;
	C2D_TargetClear(RENDER_TOP, C2D_Color32(33, 39, 43, 255));
	C2D_TargetClear(RENDER_BOTTOM, C2D_Color32(33, 39, 43, 255));

	while(aptMainLoop()) {
		hidScanInput();
		u32 kDown = hidKeysDown();

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_SceneBegin(RENDER_TOP);

		if (gif_is_animated) {
			Gallery_DrawImage(gif_image[count], ((400 - (frame_width[count] * 1.0f)) / 2), ((240 - (frame_height[count] * 1.0f)) / 2), 0, 0, 
				frame_width[count], frame_height[count], 1.0f);
			svcSleepThread(frame_times[count]);

			count++;
			if (count == gif.frame_count)
				count = 0;
		}
		else
			Gallery_DrawImage(gif_image[0], ((400 - (frame_width[0] * 1.0f)) / 2), ((240 - (frame_height[0] * 1.0f)) / 2), 0, 0, 
				frame_width[0], frame_height[0], 1.0f);

		Draw_EndFrame();
		
		if (kDown & KEY_B)
			break;
	}

	if (gif_is_animated) {
		for (unsigned int i = 0; i < gif.frame_count; i++) {
			C3D_TexDelete(gif_image[i].tex);
			linearFree((Tex3DS_SubTexture *)&gif_image[i].subtex);
		}
	}
	else {
		C3D_TexDelete(gif_image[0].tex);
		linearFree((Tex3DS_SubTexture *)&gif_image[0].subtex);
	}

	C2D_TargetClear(RENDER_TOP, C2D_Color32(33, 39, 43, 255));
	C2D_TargetClear(RENDER_BOTTOM, C2D_Color32(33, 39, 43, 255));
	gif_finalise(&gif);
	linearFree(data);
	return true;
}

void Gallery_DisplayImage(char *path) {
	Gallery_GetImageList();
	Gallery_LoadTexture(path);

	u64 last_time = osGetTime(), current_time = 0;

	float zoom_factor = 1.0f;
	pos_x = 0;
	pos_y = 0;

	while(aptMainLoop()) {
		current_time = osGetTime();
		u64 delta_time = current_time - last_time;
		last_time = current_time;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(RENDER_TOP, C2D_Color32(33, 39, 43, 255));
		C2D_TargetClear(RENDER_BOTTOM, C2D_Color32(33, 39, 43, 255));
		C2D_SceneBegin(RENDER_TOP);

		switch (dimensions) {
			case DIMENSION_DEFAULT:
				Gallery_DrawImage(image, ((400 - (width * zoom_factor)) / 2), ((240 - (height * zoom_factor)) / 2), 0, 0, width, height, zoom_factor);
				break;

			case DIMENSION_NINTENDO_SCREENSHOT:
				Gallery_DrawImage(image, 0, 0, 16, 16, image.subtex->width, image.subtex->height, 1.0f);
				break;

			case DIMENSION_3DSHELL_SCREENSHOT:
				Gallery_DrawImage(image, 0, 0, 0, 0, image.subtex->width, image.subtex->height, 1.0f);
				break;

			default:
				break;
		}

		C2D_SceneBegin(RENDER_BOTTOM);

		switch (dimensions) {
			case DIMENSION_NINTENDO_SCREENSHOT:
				Gallery_DrawImage(image, 0, 0, 56, 272, image.subtex->width, image.subtex->height, 1.0f);
				break;

			case DIMENSION_3DSHELL_SCREENSHOT:
				Gallery_DrawImage(image, 0, 0, 40, 240, image.subtex->width, image.subtex->height, 1.0f);
				break;

			default:
				break;
		}

		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();

		if ((kDown & KEY_DLEFT) || (kDown & KEY_L))
			Gallery_HandleNext(false);
		else if ((kDown & KEY_DRIGHT) || (kDown & KEY_R))
			Gallery_HandleNext(true);

		if (dimensions == DIMENSION_DEFAULT) {
			if ((height * zoom_factor > 240) || (width * zoom_factor > 400)) {
				double velocity = 2 / zoom_factor;

				if (kHeld & KEY_CPAD_UP)
					pos_y -= ((velocity * zoom_factor) * delta_time);
				else if (kHeld & KEY_CPAD_DOWN)
					pos_y += ((velocity * zoom_factor) * delta_time);
				else if (kHeld & KEY_CPAD_LEFT)
					pos_x -= ((velocity * zoom_factor) * delta_time);
				else if (kHeld & KEY_CPAD_RIGHT)
					pos_x += ((velocity * zoom_factor) * delta_time);
			}

			if ((kHeld & KEY_DUP) || (kHeld & KEY_CSTICK_UP)) {
				zoom_factor += 0.5f * (delta_time * 0.001);

				if (zoom_factor > 2.0f)
					zoom_factor = 2.0f;
			}
			else if ((kHeld & KEY_DDOWN) || (kHeld & KEY_CSTICK_DOWN)) {
				zoom_factor -= 0.5f * (delta_time * 0.001);

				if (zoom_factor < 0.5f)
					zoom_factor = 0.5f;

				if (zoom_factor <= 1.0f) {
					pos_x = 0;
					pos_y = 0;
				}
			}

			if (kDown & KEY_SELECT) { // Reset zoom/pos
				pos_x = 0;
				pos_y = 0;
				zoom_factor = 1.0f;
			}
		}

		Utils_SetMax(&pos_x, width, width);
		Utils_SetMin(&pos_x, -width, -width);
		Utils_SetMax(&pos_y, height, height);
		Utils_SetMin(&pos_y, -height, -height);

		Draw_EndFrame();

		if (kDown & KEY_B)
			break;
	}

	Gallery_FreeImage(&image);
	memset(album, 0, sizeof(album[0][0]) * 512 * 512);
	count = 0;
	MENU_STATE = MENU_STATE_HOME;
}
