#ifndef GAME_EDITOR_QUAD_KNIFE_H
#define GAME_EDITOR_QUAD_KNIFE_H

#include "component.h"

class CQuadKnife : public CEditorComponent
{
public:
	class CState
	{
	public:
		bool m_Active;
		bool m_Dragging;
		int m_GroupIndex, m_LayerIndex, m_QuadIndex;
		vec2 m_Start;

		void Reset();
	};

	bool IsActive() const;
	void Activate(int SelectedQuad);
	void Deactivate();
	void DoSlice();
	void OnUpdate() override;
};

#endif
