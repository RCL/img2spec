class ZXNextDevice : public Device
{
public:
	// ZX Spectrum Next bitmap screens. All modes use 9-bit RGB333 color; saves are
	// a 512 byte palette block (nextreg $44 byte pairs: %RRRGGGBB, %0000000B) followed
	// by the bitmap in the mode's native memory layout.
	//
	// mode  resolution  depth  layout
	//  0    256x192     8bpp   y*256+x                       (Layer 2)
	//  1    320x256     8bpp   x*256+y, column major         (Layer 2)
	//  2    640x256     4bpp   (x/2)*256+y, high nibble left (Layer 2)
	//  3    128x96      8bpp   y*128+x, $4000/$6000 halves contiguous (LoRes)
	//  4    128x96      4bpp   (y*128+x)/2, high nibble left (Radastan)

	int mOptMode;
	int mOptPaletteMode; // 0 = optimized for image, 1 = fixed default

	int mPalCount;
	int mPalRGB[256];            // expanded 0xRRGGBB for display
	unsigned char mPal9[256][3]; // 3 bit channels
	unsigned char mLut[512];     // 9 bit color -> palette index
	unsigned char mSaveBitmap[81920];

	virtual char *getname() { return "ZXNext"; }

	static int mode_width(int m)    { static const int w[5] = { 256, 320, 640, 128, 128 }; return w[m]; }
	static int mode_height(int m)   { static const int h[5] = { 192, 256, 256, 96, 96 }; return h[m]; }
	static int mode_colors(int m)   { return (m == 2 || m == 4) ? 16 : 256; }
	static int mode_savesize(int m) { static const int s[5] = { 49152, 81920, 81920, 12288, 6144 }; return s[m]; }

	ZXNextDevice()
	{
		mOptMode = 0;
		mOptPaletteMode = 0;
		mXRes = mode_width(0);
		mYRes = mode_height(0);
		mPalCount = 0;
		memset(mSaveBitmap, 0, sizeof(mSaveBitmap));
		default_palette();
		build_lut();
	}

	static int expand3(int v) { return (v * 255) / 7; }

	// note: the app's pixel ints are ABGR (GL byte order) - red in the low byte
	static int rgb333(int c)
	{
		int r = (((c >> 0) & 0xff) * 7 + 127) / 255;
		int g = (((c >> 8) & 0xff) * 7 + 127) / 255;
		int b = (((c >> 16) & 0xff) * 7 + 127) / 255;
		return (r << 6) | (g << 3) | b;
	}

	void set_pal(int i, int r, int g, int b) // 3 bit channels, true color order
	{
		mPal9[i][0] = r;
		mPal9[i][1] = g;
		mPal9[i][2] = b;
		mPalRGB[i] = (expand3(b) << 16) | (expand3(g) << 8) | expand3(r);
	}

	void default_palette()
	{
		int i;
		if (mode_colors(mOptMode) == 256)
		{
			// default Next palette: index %RRRGGGBB, blue LSB = B1|B0
			for (i = 0; i < 256; i++)
			{
				int bb = i & 3;
				set_pal(i, (i >> 5) & 7, (i >> 2) & 7, (bb << 1) | (bb ? 1 : 0));
			}
			mPalCount = 256;
		}
		else
		{
			// standard speccy 16: normal at 5/7ths, bright at full
			for (i = 0; i < 16; i++)
			{
				int v = (i & 8) ? 7 : 5;
				set_pal(i, (i & 2) ? v : 0, (i & 4) ? v : 0, (i & 1) ? v : 0);
			}
			mPalCount = 16;
		}
	}

	void build_lut()
	{
		int c, i;
		for (c = 0; c < 512; c++)
		{
			int r = expand3((c >> 6) & 7);
			int g = expand3((c >> 3) & 7);
			int b = expand3(c & 7);
			int best = 0, bestdist = 256 * 256 * 4;
			for (i = 0; i < mPalCount; i++)
			{
				int dr = r - expand3(mPal9[i][0]);
				int dg = g - expand3(mPal9[i][1]);
				int db = b - expand3(mPal9[i][2]);
				int dist = dr * dr + dg * dg + db * db;
				if (dist < bestdist)
				{
					bestdist = dist;
					best = i;
				}
			}
			mLut[c] = (unsigned char)best;
		}
	}

	void optimized_palette(int *aCounts)
	{
		// median cut over the 512 bin histogram
		int order[512];
		int used = 0;
		int i, j;
		for (i = 0; i < 512; i++)
			if (aCounts[i])
				order[used++] = i;

		int boxstart[256], boxlen[256];
		int boxes = 1;
		boxstart[0] = 0;
		boxlen[0] = used;
		int target = mode_colors(mOptMode);

		while (boxes < target)
		{
			// pick the most populated box that still has more than one color
			int pick = -1, pickpop = 0;
			for (i = 0; i < boxes; i++)
			{
				if (boxlen[i] < 2) continue;
				int pop = 0;
				for (j = 0; j < boxlen[i]; j++)
					pop += aCounts[order[boxstart[i] + j]];
				if (pop > pickpop)
				{
					pickpop = pop;
					pick = i;
				}
			}
			if (pick < 0) break;

			// find widest channel in the picked box
			int mins[3] = { 8, 8, 8 }, maxs[3] = { -1, -1, -1 };
			for (j = 0; j < boxlen[pick]; j++)
			{
				int c = order[boxstart[pick] + j];
				int ch[3] = { (c >> 6) & 7, (c >> 3) & 7, c & 7 };
				for (i = 0; i < 3; i++)
				{
					if (ch[i] < mins[i]) mins[i] = ch[i];
					if (ch[i] > maxs[i]) maxs[i] = ch[i];
				}
			}
			int axis = 0;
			if (maxs[1] - mins[1] >= maxs[axis] - mins[axis]) axis = 1;
			if (maxs[2] - mins[2] > maxs[axis] - mins[axis]) axis = 2;
			int shift = (axis == 0) ? 6 : (axis == 1) ? 3 : 0;

			// insertion sort the box content along that axis
			for (i = boxstart[pick] + 1; i < boxstart[pick] + boxlen[pick]; i++)
			{
				int v = order[i];
				for (j = i; j > boxstart[pick] && ((order[j - 1] >> shift) & 7) > ((v >> shift) & 7); j--)
					order[j] = order[j - 1];
				order[j] = v;
			}

			// split at the weighted median
			int half = 0;
			for (j = 0; j < boxlen[pick]; j++)
				half += aCounts[order[boxstart[pick] + j]];
			half /= 2;
			int acc = 0, cut = 1;
			for (j = 0; j < boxlen[pick] - 1; j++)
			{
				acc += aCounts[order[boxstart[pick] + j]];
				if (acc >= half)
				{
					cut = j + 1;
					break;
				}
			}
			boxstart[boxes] = boxstart[pick] + cut;
			boxlen[boxes] = boxlen[pick] - cut;
			boxlen[pick] = cut;
			boxes++;
		}

		// weighted average of each box, rounded back to 3 bit channels
		for (i = 0; i < boxes; i++)
		{
			int sum[3] = { 0, 0, 0 };
			int pop = 0;
			for (j = 0; j < boxlen[i]; j++)
			{
				int c = order[boxstart[i] + j];
				int n = aCounts[c];
				sum[0] += ((c >> 6) & 7) * n;
				sum[1] += ((c >> 3) & 7) * n;
				sum[2] += (c & 7) * n;
				pop += n;
			}
			set_pal(i, (sum[0] + pop / 2) / pop, (sum[1] + pop / 2) / pop, (sum[2] + pop / 2) / pop);
		}
		mPalCount = boxes;
	}

	virtual int estimate_rgb(int c)
	{
		return mPalRGB[mLut[rgb333(c)]] | 0xff000000;
	}

	void pack_bitmap()
	{
		int x, y;
		memset(mSaveBitmap, 0, sizeof(mSaveBitmap));
		for (y = 0; y < mYRes; y++)
		{
			for (x = 0; x < mXRes; x++)
			{
				int idx = gBitmapSpec[y * mXRes + x];
				switch (mOptMode)
				{
				case 0:
					mSaveBitmap[y * 256 + x] = idx;
					break;
				case 1:
					mSaveBitmap[x * 256 + y] = idx;
					break;
				case 2:
					mSaveBitmap[(x >> 1) * 256 + y] |= (x & 1) ? idx : (idx << 4);
					break;
				case 3:
					mSaveBitmap[y * 128 + x] = idx;
					break;
				case 4:
					mSaveBitmap[(y * 128 + x) >> 1] |= (x & 1) ? idx : (idx << 4);
					break;
				}
			}
		}
	}

	virtual void filter()
	{
		int i;
		int npix = mXRes * mYRes;

		if (mOptPaletteMode == 1)
		{
			default_palette();
		}
		else
		{
			int counts[512];
			for (i = 0; i < 512; i++)
				counts[i] = 0;
			for (i = 0; i < npix; i++)
				counts[rgb333(gBitmapProc[i])]++;
			optimized_palette(counts);
		}
		build_lut();

		for (i = 0; i < npix; i++)
			gBitmapSpec[i] = mLut[rgb333(gBitmapProc[i])];

		pack_bitmap();

		for (i = 0; i < npix; i++)
			gBitmapSpec[i] = mPalRGB[gBitmapSpec[i]] | 0xff000000;
	}

	void write_palette(unsigned char *aOut)
	{
		int i;
		memset(aOut, 0, 512);
		for (i = 0; i < mPalCount; i++)
		{
			aOut[i * 2 + 0] = (mPal9[i][0] << 5) | (mPal9[i][1] << 2) | (mPal9[i][2] >> 1);
			aOut[i * 2 + 1] = mPal9[i][2] & 1;
		}
	}

	virtual void savescr(FILE * f)
	{
		unsigned char pal[512];
		write_palette(pal);
		fwrite(pal, 512, 1, f);
		fwrite(mSaveBitmap, mode_savesize(mOptMode), 1, f);
	}

	virtual void saveh(FILE * f)
	{
		unsigned char pal[512];
		write_palette(pal);
		int i, c = 0;
		for (i = 0; i < 512; i++)
		{
			fprintf(f, "%3u,", pal[i]);
			c++;
			if (c >= 32)
			{
				fprintf(f, "\n");
				c = 0;
			}
		}
		fprintf(f, "\n\n");
		c = 0;
		int bytes = mode_savesize(mOptMode);
		for (i = 0; i < bytes; i++)
		{
			fprintf(f, "%3u%s", mSaveBitmap[i], i != bytes - 1 ? "," : "");
			c++;
			if (c >= 32)
			{
				fprintf(f, "\n");
				c = 0;
			}
		}
	}

	virtual void saveinc(FILE * f)
	{
		unsigned char pal[512];
		write_palette(pal);
		int i;
		for (i = 0; i < 512; i++)
		{
			fprintf(f, "\t.db #0x%02x\n", pal[i]);
		}
		fprintf(f, "\n\n");
		for (i = 0; i < mode_savesize(mOptMode); i++)
		{
			fprintf(f, "\t.db #0x%02x\n", mSaveBitmap[i]);
		}
	}

	virtual void attr_bitm()
	{
		// left: computed palette as a 16x16 swatch grid; right: pixel indices as grayscale
		int i, j;
		int cw = mXRes / 16, chh = mYRes / 16;
		for (i = 0; i < mYRes; i++)
		{
			for (j = 0; j < mXRes; j++)
			{
				int pi = (i / chh) * 16 + (j / cw);
				gBitmapAttr[i * mXRes + j] = (pi < mPalCount ? mPalRGB[pi] : 0) | 0xff000000;
				int idx = mLut[rgb333(gBitmapProc[i * mXRes + j])];
				int v = (mode_colors(mOptMode) == 16) ? idx * 17 : idx;
				gBitmapBitm[i * mXRes + j] = 0xff000000 | (v << 16) | (v << 8) | v;
			}
		}
		update_texture(gTextureAttr, gBitmapAttr);
		update_texture(gTextureBitm, gBitmapBitm);

		ImVec2 picsize((float)mXRes, (float)mYRes);
		ImGui::Image((ImTextureID)gTextureAttr, picsize, ImVec2(0, 0), ImVec2(mXRes / 1024.0f, mYRes / 512.0f)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
		ImGui::Image((ImTextureID)gTextureBitm, picsize, ImVec2(0, 0), ImVec2(mXRes / 1024.0f, mYRes / 512.0f)); ImGui::SameLine(); ImGui::Text(" ");
	}

	virtual void options()
	{
		if (ImGui::Combo("Screen mode", &mOptMode, "Layer 2 256x192 (256 colors)\0Layer 2 320x256 (256 colors)\0Layer 2 640x256 (16 colors)\0LoRes 128x96 (256 colors)\0Radastan 128x96 (16 colors)\0"))
		{
			mXRes = mode_width(mOptMode);
			mYRes = mode_height(mOptMode);
			gDirty = 1;
			gDirtyPic = 1;
		}
		if (ImGui::Combo("Palette", &mOptPaletteMode, "Optimized for image\0Default (fixed)\0")) gDirty = 1;
	}

	virtual void zoomed(int aWhich)
	{
		int tex;
		switch (aWhich)
		{
		default: //case 0:
			tex = gTextureSpec;
			break;
		case 1:
			tex = gTextureProc;
			break;
		case 2:
			tex = gTextureOrig;
			break;
		}
		ImGui::Image((ImTextureID)tex, ImVec2((float)mXRes * gOptZoom, (float)mYRes * gOptZoom),
			ImVec2(0, 0), ImVec2(mXRes / 1024.0f, mYRes / 512.0f));
	}

	virtual void writeOptions(JSON_Object *root)
	{
#define WRITECONFIG(x) json_object_dotset_number(root, "Device." #x, x);
		WRITECONFIG(mOptMode);
		WRITECONFIG(mOptPaletteMode);
#undef WRITECONFIG
	}

	virtual void readOptions(JSON_Object *root)
	{
#define READCONFIG(x) if (json_object_dotget_value(root, "Device." #x) != NULL) x = json_object_dotget_number(root, "Device." #x);
#pragma warning(disable:4244; disable:4800)
		READCONFIG(mOptMode);
		READCONFIG(mOptPaletteMode);
#pragma warning(default:4244; default:4800)
#undef READCONFIG

		if (mOptMode < 0 || mOptMode > 4) mOptMode = 0;
		mXRes = mode_width(mOptMode);
		mYRes = mode_height(mOptMode);

		gDirty = 1;
		gDirtyPic = 1;
	}
};
