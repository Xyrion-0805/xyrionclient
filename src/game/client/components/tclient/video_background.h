#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_VIDEO_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_VIDEO_BACKGROUND_H

#include <memory>
#include <string>

class IGraphics;
class IStorage;

// Decoder work is isolated from the graphics thread. Call Shutdown while
// graphics and storage are still alive (also done when leaving the menu).
// Render/Shutdown must run on the main thread. Render reuses an RGBA texture
// through IGraphics::UpdateTextureRawMove; creation uses TEXLOAD_NO_MIPMAPS.
// Decode/convert/upload counters are logged under video_background/perf every
// five seconds and on shutdown (upload timing measures CPU enqueue only).
class CVideoBackground
{
	class CImpl;
	std::unique_ptr<CImpl> m_pImpl;

public:
	CVideoBackground();
	~CVideoBackground();

	CVideoBackground(const CVideoBackground &) = delete;
	CVideoBackground &operator=(const CVideoBackground &) = delete;

	bool Render(IGraphics *pGraphics, IStorage *pStorage, bool Enabled, const char *pPath, int Dim);
	void Shutdown(IGraphics *pGraphics);
	std::string GetStatus() const;
	// Checks the loaded FFmpeg library's MP4 demuxer/decoder capabilities,
	// since DDNet's recorder-only bundles do not necessarily include them.
	static bool IsSupported();
};

#endif
