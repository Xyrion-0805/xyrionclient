/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_TEXTRENDER_H
#define ENGINE_TEXTRENDER_H
#include "kernel.h"

#include <base/color.h>

#include <engine/graphics.h>

#include <cstdint>
#include <memory>
#include <string> // TClient

enum
{
	TEXTFLAG_RENDER = 1 << 0,
	TEXTFLAG_DISALLOW_NEWLINE = 1 << 1,
	TEXTFLAG_STOP_AT_END = 1 << 2,
	TEXTFLAG_ELLIPSIS_AT_END = 1 << 3,
};

enum ETextAlignment
{
	TEXTALIGN_LEFT = 0,
	TEXTALIGN_CENTER = 1 << 1,
	TEXTALIGN_RIGHT = 1 << 2,
	TEXTALIGN_TOP = 0, // this is also 0, so the default alignment is top-left
	TEXTALIGN_MIDDLE = 1 << 3,
	TEXTALIGN_BOTTOM = 1 << 4,

	TEXTALIGN_TL = TEXTALIGN_TOP | TEXTALIGN_LEFT,
	TEXTALIGN_TC = TEXTALIGN_TOP | TEXTALIGN_CENTER,
	TEXTALIGN_TR = TEXTALIGN_TOP | TEXTALIGN_RIGHT,
	TEXTALIGN_ML = TEXTALIGN_MIDDLE | TEXTALIGN_LEFT,
	TEXTALIGN_MC = TEXTALIGN_MIDDLE | TEXTALIGN_CENTER,
	TEXTALIGN_MR = TEXTALIGN_MIDDLE | TEXTALIGN_RIGHT,
	TEXTALIGN_BL = TEXTALIGN_BOTTOM | TEXTALIGN_LEFT,
	TEXTALIGN_BC = TEXTALIGN_BOTTOM | TEXTALIGN_CENTER,
	TEXTALIGN_BR = TEXTALIGN_BOTTOM | TEXTALIGN_RIGHT,

	TEXTALIGN_MASK_HORIZONTAL = TEXTALIGN_LEFT | TEXTALIGN_CENTER | TEXTALIGN_RIGHT,
	TEXTALIGN_MASK_VERTICAL = TEXTALIGN_TOP | TEXTALIGN_MIDDLE | TEXTALIGN_BOTTOM,
};

enum ETextRenderFlags
{
	TEXT_RENDER_FLAG_NO_X_BEARING = 1 << 0,
	TEXT_RENDER_FLAG_NO_Y_BEARING = 1 << 1,
	TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH = 1 << 2,
	TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT = 1 << 3,
	TEXT_RENDER_FLAG_KERNING = 1 << 4,
	TEXT_RENDER_FLAG_NO_OVERSIZE = 1 << 5,
	TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING = 1 << 6,
	TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE = 1 << 7,
	TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD = 1 << 8,
	// text is only rendered once and then discarded (a hint for buffer creation)
	TEXT_RENDER_FLAG_ONE_TIME_USE = 1 << 9,
};

enum class EFontPreset
{
	DEFAULT_FONT,
	ICON_FONT,
	QM_ICON_FONT,
	QM_ICON_FONT_FILL,
};

enum ETextCursorSelectionMode
{
	// ignore any kind of selection
	TEXT_CURSOR_SELECTION_MODE_NONE = 0,
	// calculates the selection based on the mouse press and release cursor position
	TEXT_CURSOR_SELECTION_MODE_CALCULATE,
	// sets the selection based on the character start and end count(these values have to be decoded character offsets)
	TEXT_CURSOR_SELECTION_MODE_SET,
};

enum ETextCursorCursorMode
{
	// ignore any kind of cursor
	TEXT_CURSOR_CURSOR_MODE_NONE = 0,
	// calculates the cursor based on the mouse release cursor position
	TEXT_CURSOR_CURSOR_MODE_CALCULATE,
	// sets the cursor based on the current character (this value has to be decoded character offset)
	TEXT_CURSOR_CURSOR_MODE_SET,
};

struct STextBoundingBox
{
	float m_X;
	float m_Y;
	float m_W;
	float m_H;
	float m_MaxCharacterHeight = 0.0f;
	int m_LineCount = 1;

	float Right() const { return m_X + m_W; }
	float Bottom() const { return m_Y + m_H; }
	vec2 Size() const { return vec2(m_W, m_H); }
	void MoveBy(vec2 Offset)
	{
		m_X += Offset.x;
		m_Y += Offset.y;
	}
};

// Allow to render multi colored text in one go without having to call TextEx() multiple times.
// Needed to allow multi colored multi line texts
struct STextColorSplit
{
	int m_CharIndex; // Which index within the text should the split occur
	int m_Length; // How long is the split
	ColorRGBA m_Color; // The color the text should be starting from m_CharIndex
	// QmClient：字符内横向渐变的右边缘色。默认等于 m_Color，此时顶点色与旧行为逐字节一致。
	ColorRGBA m_ColorEnd;

	STextColorSplit(int CharIndex, int Length, const ColorRGBA &Color) :
		m_CharIndex(CharIndex), m_Length(Length), m_Color(Color), m_ColorEnd(Color) {}

	// QmClient：显式指定左右边缘色，使单个字符内部产生横向渐变。
	STextColorSplit(int CharIndex, int Length, const ColorRGBA &Color, const ColorRGBA &ColorEnd) :
		m_CharIndex(CharIndex), m_Length(Length), m_Color(Color), m_ColorEnd(ColorEnd) {}
};

// QmClient：逐字符顶点变换，用于浮动和缩放回弹。
// 变换只作用于渲染顶点，不参与布局、断行、选区与光标计算，因此文字宽度和对齐保持稳定。
struct STextCharOffset
{
	int m_CharIndex; // 文本中的字符序号，与 STextColorSplit 使用同一套序号
	float m_XOffset;
	float m_YOffset;
	float m_Scale;

	STextCharOffset(int CharIndex, float XOffset, float YOffset, float Scale = 1.0f) :
		m_CharIndex(CharIndex), m_XOffset(XOffset), m_YOffset(YOffset), m_Scale(Scale) {}

	vec2 TransformVertex(vec2 Position, vec2 Center) const
	{
		// 默认比例沿用原顶点坐标，避免影响既有浮动效果的像素对齐。
		if(m_Scale != 1.0f)
			Position = Center + (Position - Center) * m_Scale;
		return Position + vec2(m_XOffset, m_YOffset);
	}
};

class CTextCursor
{
public:
	int m_Flags = TEXTFLAG_RENDER;
	int m_LineCount = 1;
	int m_GlyphCount = 0;
	int m_CharCount = 0;
	int m_MaxLines = 0;

	float m_StartX = 0.0f;
	float m_StartY = 0.0f;
	float m_LineWidth = -1.0f;
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_MaxCharacterHeight = 0.0f;
	bool m_CalculateVisualBoundingBox = false;
	// 按需缓存实际绘制的行范围，用于逐行扫光。
	bool m_TrackLineRanges = false;
	bool m_HasVisualBoundingBox = false;
	float m_VisualLeft = 0.0f;
	float m_VisualRight = 0.0f;
	float m_VisualTop = 0.0f;
	float m_VisualBottom = 0.0f;
	float m_LongestLineWidth = 0.0f;

	float m_FontSize = 0.0f;
	float m_AlignedFontSize = 0.0f;
	float m_LineSpacing = 0.0f;
	float m_AlignedLineSpacing = 0.0f;

	ETextCursorSelectionMode m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_NONE;
	float m_SelectionHeightFactor = 1.0f;

	// these coordinates are respected if selection mode is set to calculate @see ETextCursorSelectionMode
	vec2 m_PressMouse = vec2(0.0f, 0.0f);
	// these coordinates are respected if selection/cursor mode is set to calculate @see ETextCursorSelectionMode / @see ETextCursorCursorMode
	vec2 m_ReleaseMouse = vec2(0.0f, 0.0f);

	// note m_SelectionStart can be bigger than m_SelectionEnd, depending on how the mouse cursor was dragged
	// also note, that these are the character offsets decoded
	int m_SelectionStart = 0;
	int m_SelectionEnd = 0;

	ETextCursorCursorMode m_CursorMode = TEXT_CURSOR_CURSOR_MODE_NONE;
	bool m_ForceCursorRendering = false;
	bool m_RenderCursor = true;
	bool m_RenderSelection = true;
	std::vector<IGraphics::CQuadItem> m_vSelectionQuads;
	// note this is the decoded character offset
	int m_CursorCharacter = -1;
	vec2 m_CursorRenderedPosition = vec2(-1.0f, -1.0f);
	bool m_HasCursorRenderedPosition = false;

	/**
	 * Whether the text was truncated with @link TEXTFLAG_STOP_AT_END @endlink or @link TEXTFLAG_ELLIPSIS_AT_END @endlink being set.
	 */
	bool m_Truncated = false;

	// Color splits of the cursor to allow multicolored text
	std::vector<STextColorSplit> m_vColorSplits;
	// 渐变取色仅在同步创建字形顶点时调用；上下文须存活到 TextEx/CreateTextContainer 返回。
	using FColorSampler = ColorRGBA (*)(vec2 Position, const void *pContext);
	FColorSampler m_pfnColorSampler = nullptr;
	const void *m_pColorSamplerContext = nullptr;
	int m_ColorSamplerColumns = 8;
	int m_ColorSamplerRows = 8;

	// QmClient：逐字符顶点偏移。按字符序号升序排列且覆盖每个字符，未偏移的字符填 0，
	// 否则游标推进方式（顺序消费）会与字符错位。
	std::vector<STextCharOffset> m_vCharOffsets;


	float Height() const;
	STextBoundingBox BoundingBox() const;
	void SetPosition(vec2 Position)
	{
		m_StartX = Position.x;
		m_StartY = Position.y;
		m_X = Position.x;
		m_Y = Position.y;
	}
};

struct STextContainerUsages
{
	int m_Dummy = 0;
};

struct STextContainerIndex
{
	int m_Index;
	std::shared_ptr<STextContainerUsages> m_UseCount =
		std::make_shared<STextContainerUsages>(STextContainerUsages());

	STextContainerIndex() { Reset(); }
	bool Valid() const { return m_Index >= 0; }
	void Reset() { m_Index = -1; }
};

struct STextSizeProperties
{
	float *m_pHeight = nullptr;
	float *m_pAlignedFontSize = nullptr;
	float *m_pMaxCharacterHeightInLine = nullptr;
	float *m_pVisualTop = nullptr;
	float *m_pVisualBottom = nullptr;
	int *m_pLineCount = nullptr;
};

class ITextRender : public IInterface
{
	MACRO_INTERFACE("textrender")
public:
	virtual std::vector<std::string> *GetCustomFaces() = 0; // TClient
	virtual void SetCustomFace(const char *pFace) = 0; // TClient

	virtual bool LoadFonts() = 0;
	virtual void SetFontPreset(EFontPreset FontPreset) = 0;
	virtual EFontPreset GetFontPreset() const = 0;
	virtual uint64_t GlyphAtlasRevision() const = 0;
	virtual void SetFontPreviewFace(const char *pFace) = 0;
	virtual bool QmHasCustomFace(const char *pFace) = 0;
	virtual bool QmFontFamilyDefaultConfig(const char *pFamily, std::string &Config) = 0;
	virtual void SetFontLanguageVariant(const char *pLanguageFile) = 0;

	virtual void SetRenderFlags(unsigned Flags) = 0;
	virtual unsigned GetRenderFlags() const = 0;

	ColorRGBA DefaultTextColor() const { return ColorRGBA(1, 1, 1, 1); }
	ColorRGBA DefaultTextOutlineColor() const { return ColorRGBA(0, 0, 0, 0.3f); }
	ColorRGBA DefaultTextSelectionColor() const { return ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f); }

	//
	virtual void TextEx(CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual bool CreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual void AppendTextContainer(STextContainerIndex TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	// either creates a new text container or appends to a existing one
	virtual bool CreateOrAppendTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	// just deletes and creates text container
	virtual void RecreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual void RecreateTextContainerSoft(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual void DeleteTextContainer(STextContainerIndex &TextContainerIndex) = 0;

	virtual void UploadTextContainer(STextContainerIndex TextContainerIndex) = 0;

	virtual void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) = 0;
	virtual void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor, float X, float Y) = 0;

	virtual STextBoundingBox GetBoundingBoxTextContainer(STextContainerIndex TextContainerIndex) = 0;

	virtual void UploadEntityLayerText(const CImageInfo &TextImage, int TexSubWidth, int TexSubHeight, const char *pText, int Length, float x, float y, int FontSize) = 0;
	virtual int AdjustFontSize(const char *pText, int TextLength, int MaxSize, int MaxWidth) const = 0;
	virtual float GetGlyphOffsetX(int FontSize, char TextCharacter) const = 0;
	virtual int CalculateTextWidth(const char *pText, int TextLength, int FontWidth, int FontSize) const = 0;

	// old foolish interface
	virtual void TextColor(float r, float g, float b, float a) = 0;
	virtual void TextColor(ColorRGBA Color) = 0;
	virtual void TextOutlineColor(float r, float g, float b, float a) = 0;
	virtual void TextOutlineColor(ColorRGBA Color) = 0;
	virtual void TextSelectionColor(float r, float g, float b, float a) = 0;
	virtual void TextSelectionColor(ColorRGBA Color) = 0;
	virtual void Text(float x, float y, float Size, const char *pText, float LineWidth = -1.0f) = 0;
	virtual float TextWidth(float Size, const char *pText, int StrLength = -1, float LineWidth = -1.0f, int Flags = 0, const STextSizeProperties &TextSizeProps = {}) = 0;
	virtual STextBoundingBox TextBoundingBox(float Size, const char *pText, int StrLength = -1, float LineWidth = -1.0f, float LineSpacing = 0.0f, int Flags = 0) = 0;

	virtual ColorRGBA GetTextColor() const = 0;
	virtual ColorRGBA GetTextOutlineColor() const = 0;
	virtual ColorRGBA GetTextSelectionColor() const = 0;

	virtual void OnPreWindowResize() = 0;
	virtual void OnWindowResize() = 0;
};

class IEngineTextRender : public ITextRender
{
	MACRO_INTERFACE("enginetextrender")
public:
	virtual void Init() = 0;
	void Shutdown() override = 0;
};

extern IEngineTextRender *CreateEngineTextRender();

#endif
