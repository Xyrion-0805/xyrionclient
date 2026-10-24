// GPU backdrop sampling and yzrt-inspired UI material; called on the render thread.
void CCommandProcessorFragment_OpenGL::ShutdownUiGlass()
{
#ifndef BACKEND_AS_OPENGL_ES
	if(m_UiGlassProgram) glDeleteProgram(m_UiGlassProgram);
	if(m_UiGlassTexture) glDeleteTextures(1, &m_UiGlassTexture);
	if(m_UiGlassVao) glDeleteVertexArrays(1, &m_UiGlassVao);
	m_UiGlassProgram = m_UiGlassTexture = m_UiGlassVao = 0;
#endif
}

void CCommandProcessorFragment_OpenGL::Cmd_UiGlass(const CCommandBuffer::SCommand_UiGlass *pCommand)
{
#ifndef BACKEND_AS_OPENGL_ES
	if(m_UiGlassFailed || !m_pUiGlassStorage) return;
	if(!m_UiGlassProgram)
	{
		auto Compile = [&](const char *pPath, GLenum Type) {
			char *pSource = m_pUiGlassStorage->ReadFileStr(pPath, IStorage::TYPE_ALL);
			if(!pSource) return GLuint(0);
			GLuint Shader = glCreateShader(Type);
			glShaderSource(Shader, 1, &pSource, nullptr);
			glCompileShader(Shader);
			free(pSource);
			GLint Success = 0; glGetShaderiv(Shader, GL_COMPILE_STATUS, &Success);
			if(!Success) { char aLog[1024]; glGetShaderInfoLog(Shader, sizeof(aLog), nullptr, aLog); log_error("ui_glass", "%s", aLog); glDeleteShader(Shader); return GLuint(0); }
			return Shader;
		};
		GLuint Vertex = Compile("shader/ui_glass.vert", GL_VERTEX_SHADER), Fragment = Compile("shader/ui_glass.frag", GL_FRAGMENT_SHADER);
		if(Vertex && Fragment)
		{
			m_UiGlassProgram = glCreateProgram();
			glAttachShader(m_UiGlassProgram, Vertex); glAttachShader(m_UiGlassProgram, Fragment); glLinkProgram(m_UiGlassProgram);
			GLint Linked = 0; glGetProgramiv(m_UiGlassProgram, GL_LINK_STATUS, &Linked);
			if(!Linked) { glDeleteProgram(m_UiGlassProgram); m_UiGlassProgram = 0; }
		}
		if(Vertex) glDeleteShader(Vertex);
		if(Fragment) glDeleteShader(Fragment);
		if(!m_UiGlassProgram) { m_UiGlassFailed = true; return; }
		glGenTextures(1, &m_UiGlassTexture); glGenVertexArrays(1, &m_UiGlassVao);
		log_info("ui_glass", "Loaded yzrt CSS-layer global UI material (blur, inset highlights, black shadows)");
	}
	SetState(pCommand->m_State);
	GLint Program, Vao, Active, Texture, Sampler = 0, aViewport[4];
	glGetIntegerv(GL_CURRENT_PROGRAM, &Program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Vao); glGetIntegerv(GL_ACTIVE_TEXTURE, &Active);
	glGetIntegerv(GL_VIEWPORT, aViewport);
	const int W = aViewport[2], H = aViewport[3];
	if(W <= 0 || H <= 0) return;
	glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &Texture);
	if(glBindSampler) { glGetIntegerv(GL_SAMPLER_BINDING, &Sampler); glBindSampler(0, 0); }
	glBindTexture(GL_TEXTURE_2D, m_UiGlassTexture);
	if(W != m_UiGlassW || H != m_UiGlassH)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		m_UiGlassW = W; m_UiGlassH = H; m_UiGlassFrame = UINT64_MAX;
	}
	if(m_UiGlassFrame != pCommand->m_Frame)
	{
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, aViewport[0], aViewport[1], W, H);
		m_UiGlassFrame = pCommand->m_Frame;
	}
	const auto &State = pCommand->m_State;
	const float SX = W / (State.m_ScreenBR.x - State.m_ScreenTL.x), SY = H / (State.m_ScreenBR.y - State.m_ScreenTL.y);
	glUseProgram(m_UiGlassProgram); glBindVertexArray(m_UiGlassVao);
	glUniform1i(glGetUniformLocation(m_UiGlassProgram, "gScene"), 0);
	glUniform2f(glGetUniformLocation(m_UiGlassProgram, "gViewport"), W, H);
	glUniform2f(glGetUniformLocation(m_UiGlassProgram, "gOrigin"), aViewport[0], aViewport[1]);
	glUniform4f(glGetUniformLocation(m_UiGlassProgram, "gPanel"), (pCommand->m_X - State.m_ScreenTL.x) * SX, (pCommand->m_Y - State.m_ScreenTL.y) * SY, pCommand->m_W * SX, pCommand->m_H * SY);
	glUniform1f(glGetUniformLocation(m_UiGlassProgram, "gRadius"), pCommand->m_Radius * std::min(SX, SY));
	glUniform1f(glGetUniformLocation(m_UiGlassProgram, "gOpacity"), pCommand->m_Opacity);
	const auto &Color = pCommand->m_Color;
	glUniform4f(glGetUniformLocation(m_UiGlassProgram, "gTint"), Color.r, Color.g, Color.b, Color.a);
	const auto &GlassColor = pCommand->m_GlassColor;
	glUniform3f(glGetUniformLocation(m_UiGlassProgram, "gGlassColor"), GlassColor.r, GlassColor.g, GlassColor.b);
	glUniform4f(glGetUniformLocation(m_UiGlassProgram, "gCorners"), (pCommand->m_Corners & IGraphics::CORNER_TL) != 0, (pCommand->m_Corners & IGraphics::CORNER_TR) != 0, (pCommand->m_Corners & IGraphics::CORNER_BL) != 0, (pCommand->m_Corners & IGraphics::CORNER_BR) != 0);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(Vao); glUseProgram(Program); glBindTexture(GL_TEXTURE_2D, Texture);
	if(glBindSampler) glBindSampler(0, Sampler);
	glActiveTexture(Active);
#endif
}
