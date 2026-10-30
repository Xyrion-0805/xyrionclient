#include "quad_knife.h"

#include "editor.h"
#include "editor_actions.h"
#include "localization.h"
#include "quad_crop.h"

#include <engine/keys.h>

void CQuadKnife::CState::Reset()
{
	m_Active = false;
	m_Dragging = false;
	m_GroupIndex = m_LayerIndex = m_QuadIndex = -1;
	m_Start = vec2(0, 0);
}

bool CQuadKnife::IsActive() const
{
	return Map()->m_QuadKnifeState.m_Active;
}

void CQuadKnife::Activate(int SelectedQuad)
{
	Deactivate();
	const auto pLayer = std::static_pointer_cast<CLayerQuads>(Map()->SelectedLayerType(0, LAYERTYPE_QUADS));
	if(!pLayer || SelectedQuad < 0 || SelectedQuad >= (int)Map()->m_vSelectedQuads.size()) return;
	const int QuadIndex = Map()->m_vSelectedQuads[SelectedQuad];
	if(QuadIndex < 0 || QuadIndex >= (int)pLayer->m_vQuads.size()) return;
	auto &State = Map()->m_QuadKnifeState;
	State.m_Active = true;
	State.m_GroupIndex = Map()->m_SelectedGroup;
	State.m_LayerIndex = Map()->m_vSelectedLayers[0];
	State.m_QuadIndex = QuadIndex;
}

void CQuadKnife::Deactivate()
{
	if(Ui()->ActiveItem() == &Map()->m_QuadKnifeState) Ui()->SetActiveItem(nullptr);
	Map()->m_QuadKnifeState.Reset();
}

void CQuadKnife::OnUpdate()
{
	if(!IsActive()) return;
	const auto &State = Map()->m_QuadKnifeState;
	const auto pLayer = std::static_pointer_cast<CLayerQuads>(Map()->SelectedLayerType(0, LAYERTYPE_QUADS));
	if(Map()->m_SelectedGroup != State.m_GroupIndex || Map()->m_vSelectedLayers.empty() ||
		Map()->m_vSelectedLayers[0] != State.m_LayerIndex || !pLayer || State.m_QuadIndex < 0 || State.m_QuadIndex >= (int)pLayer->m_vQuads.size() ||
		std::find(Map()->m_vSelectedQuads.begin(), Map()->m_vSelectedQuads.end(), State.m_QuadIndex) == Map()->m_vSelectedQuads.end())
		Deactivate();
}

void CQuadKnife::DoSlice()
{
	OnUpdate();
	if(!IsActive()) return;
	auto &State = Map()->m_QuadKnifeState;
	if(Editor()->m_Dialog != DIALOG_NONE || Ui()->IsPopupOpen())
	{
		State.m_Dragging = false;
		if(Ui()->ActiveItem() == &State) Ui()->SetActiveItem(nullptr);
		return;
	}
	str_copy(Editor()->m_aTooltip, EditorLocalize("Drag with the left mouse button to crop a rectangle. Release to create the slice. Hold Alt to ignore grid. Right click or Escape to cancel."));
	if(Ui()->MouseButtonClicked(1) || Input()->KeyPress(KEY_ESCAPE))
	{
		Deactivate();
		return;
	}
	const auto pLayer = std::static_pointer_cast<CLayerQuads>(Map()->SelectedLayerType(0, LAYERTYPE_QUADS));
	// Copy before appending slices: vector growth invalidates pointers to quads.
	const CQuad Source = pLayer->m_vQuads[State.m_QuadIndex];
	vec2 Point = Editor()->MapView()->MouseWorldPos();
	if(Editor()->MapView()->MapGrid()->IsEnabled() && !Input()->AltIsPressed())
		Editor()->MapView()->MapGrid()->SnapToGrid(Point);
	if(Ui()->HotItem() == Editor()->MapView() && Ui()->MouseButtonClicked(0) && !Input()->ModifierIsPressed() &&
		Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::NONE)
	{
		State.m_Start = Point;
		State.m_Dragging = true;
	}
	std::vector<CQuad> vSlices;
	if(State.m_Dragging)
	{
		Ui()->SetActiveItem(&State);
		vSlices = CropQuadToRectangle(Source, State.m_Start, Point);
		if(!Ui()->MouseButton(0))
		{
			State.m_Dragging = false;
			Ui()->SetActiveItem(nullptr);
			if(!vSlices.empty())
			{
				Map()->m_EditorHistory.BeginBulk();
				for(const auto &Slice : vSlices)
				{
					pLayer->m_vQuads.push_back(Slice);
					Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionNewQuad>(Map(), State.m_GroupIndex, State.m_LayerIndex));
				}
				Map()->OnModify();
				Map()->m_EditorHistory.EndBulk("Slice quad using rectangle");
			}
		}
	}

	Graphics()->TextureClear();
	if(State.m_Dragging)
	{
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 0.7f, 0.3f, 0.22f);
		for(const auto &Slice : vSlices)
		{
			const auto *p = Slice.m_aPoints;
			IGraphics::CFreeformItem Fill(fx2f(p[0].x), fx2f(p[0].y), fx2f(p[1].x), fx2f(p[1].y), fx2f(p[2].x), fx2f(p[2].y), fx2f(p[3].x), fx2f(p[3].y));
			Graphics()->QuadsDrawFreeform(&Fill, 1);
		}
		Graphics()->QuadsEnd();
	}
	Graphics()->LinesBegin();
	Graphics()->SetColor(1.0f, 0.5f, 0.0f, 1.0f);
	constexpr int aOrder[] = {0, 1, 3, 2};
	for(int I = 0; I < 4; ++I)
	{
		const CPoint &A = Source.m_aPoints[aOrder[I]], &B = Source.m_aPoints[aOrder[(I + 1) % 4]];
		IGraphics::CLineItem Line(fx2f(A.x), fx2f(A.y), fx2f(B.x), fx2f(B.y));
		Graphics()->LinesDraw(&Line, 1);
	}
	if(State.m_Dragging)
	{
		const auto Corners = QuadCropRectangle(State.m_Start, Point);
		if(vSlices.empty()) Graphics()->SetColor(1.0f, 0.3f, 0.3f, 1.0f);
		else Graphics()->SetColor(1.0f, 0.85f, 0.5f, 1.0f);
		for(int I = 0; I < 4; ++I)
		{
			IGraphics::CLineItem Line(Corners[aOrder[I]], Corners[aOrder[(I + 1) % 4]]);
			Graphics()->LinesDraw(&Line, 1);
		}
	}
	Graphics()->LinesEnd();
}
