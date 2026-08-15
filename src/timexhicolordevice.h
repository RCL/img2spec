class TimexHiColorDevice : public ZXSpectrumDevice
{
public:
	// Timex SCLD hi-color mode: standard spectrum attribute bytes (ink+paper+bright),
	// but one per 8x1 cell. Saves are bitmap followed by one attribute byte per
	// bitmap byte in linear order - with "Spectrum video RAM order" at 256x192
	// that is the standard 12288 byte .mlt layout.

	virtual char *getname() { return "TimexHiColor"; }

	TimexHiColorDevice()
	{
		mOptCellSize = 3; // 8x1, fixed by hardware
		mOptHeightCells = mYRes;
	}

	virtual void savescr(FILE * f)
	{
		unsigned char *bm = mSpectrumBitmap;
		if (mOptScreenOrder == 0)
		{
			bm = mSpectrumBitmapLinear;
		}
		fwrite(bm, (mXRes / 8) * mYRes, 1, f);
		fwrite(mSpectrumAttributes, (mXRes / 8) * mYRes, 1, f);
	}

	virtual void saveh(FILE * f)
	{
		unsigned char *bm = mSpectrumBitmap;
		if (mOptScreenOrder == 0)
		{
			bm = mSpectrumBitmapLinear;
		}
		int bytes = (mXRes / 8) * mYRes;
		int i, c = 0;
		for (i = 0; i < bytes; i++)
		{
			fprintf(f, "%3u,", bm[i]);
			c++;
			if (c >= 32)
			{
				fprintf(f, "\n");
				c = 0;
			}
		}
		fprintf(f, "\n\n");
		c = 0;
		for (i = 0; i < bytes; i++)
		{
			fprintf(f, "%3u%s", mSpectrumAttributes[i], i != bytes - 1 ? "," : "");
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
		unsigned char *bm = mSpectrumBitmap;
		if (mOptScreenOrder == 0)
		{
			bm = mSpectrumBitmapLinear;
		}
		int bytes = (mXRes / 8) * mYRes;
		int i;
		for (i = 0; i < bytes; i++)
		{
			fprintf(f, "\t.db #0x%02x\n", bm[i]);
		}
		fprintf(f, "\n\n");
		for (i = 0; i < bytes; i++)
		{
			fprintf(f, "\t.db #0x%02x\n", mSpectrumAttributes[i]);
		}
	}

	virtual void options()
	{
		if (ImGui::SliderInt("Bitmap width in cells", &mOptWidthCells, 1, 1028 / 8)) { gDirty = 1; gDirtyPic = 1; mXRes = mOptWidthCells * 8; }
		if (ImGui::SliderInt("Bitmap height in cells", &mOptHeightCells, 1, 512)) { gDirty = 1; gDirtyPic = 1; mYRes = mOptHeightCells; }
		if (ImGui::Combo("Attribute order", &mOptAttribOrder, "Make bitmap pretty\0Make bitmap compressable\0")) gDirty = 1;
		ImGui::Combo("Bitmap order when saving", &mOptScreenOrder, "Linear order\0Spectrum video RAM order\0");
		ImGui::Separator();
		if (ImGui::SliderFloat("Bright attribute bias", &mOptBright, 0, 1)) gDirty = 1;
		if (ImGui::Combo("Paper attribute", &mOptPaper, "Optimal\0Black\0Blue\0Red\0Purple\0Green\0Cyan\0Yellow\0White\0")) gDirty = 1;
		if (ImGui::Combo("Ink color for empty cells", &mOptEmptyCellInkColor, "Black\0Blue\0Red\0Purple\0Green\0Cyan\0Yellow\0White\0")) gDirty = 1;
		if (ImGui::Combo("Coversion mode", &mOptConversionMode, "Popular color\0Average - middle\0Average - median\0Average - dual median\0")) { gDirty = 1; }
		if (mOptConversionMode != 0)
		if (ImGui::SliderFloat("Pivot bias", &mOptPivotBias, 0, 1)) { gDirty = 1; }
	}

	virtual void writeOptions(JSON_Object *root)
	{
#define WRITECONFIG(x) json_object_dotset_number(root, "Device." #x, x);
		WRITECONFIG(mOptAttribOrder);
		WRITECONFIG(mOptBright);
		WRITECONFIG(mOptPaper);
		WRITECONFIG(mOptScreenOrder);
		WRITECONFIG(mOptWidthCells);
		WRITECONFIG(mOptHeightCells);
		WRITECONFIG(mOptConversionMode);
		WRITECONFIG(mOptPivotBias);
		WRITECONFIG(mOptEmptyCellInkColor);
#undef WRITECONFIG
	}

	virtual void readOptions(JSON_Object *root)
	{
#define READCONFIG(x) if (json_object_dotget_value(root, "Device." #x) != NULL) x = json_object_dotget_number(root, "Device." #x);
#pragma warning(disable:4244; disable:4800)
		READCONFIG(mOptAttribOrder);
		READCONFIG(mOptBright);
		READCONFIG(mOptPaper);
		READCONFIG(mOptScreenOrder);
		READCONFIG(mOptWidthCells);
		READCONFIG(mOptHeightCells);
		READCONFIG(mOptConversionMode);
		READCONFIG(mOptPivotBias);
		READCONFIG(mOptEmptyCellInkColor);
#pragma warning(default:4244; default:4800)
#undef READCONFIG

		mOptCellSize = 3; // 8x1, fixed by hardware
		mXRes = mOptWidthCells * 8;
		mYRes = mOptHeightCells;

		gDirty = 1;
		gDirtyPic = 1;
	}
};
