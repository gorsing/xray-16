#include "StdAfx.h"
#include "UITaskWnd.h"
#include "UIMapWnd.h"
#include "Common/object_broker.h"
#include "UIXmlInit.h"
#include "xrUICore/Static/UIStatic.h"
#include "xrUICore/Buttons/UI3tButton.h"
#include "xrUICore/Windows/UIFrameLineWnd.h"
#include "UISecondTaskWnd.h"
#include "UIMapLegend.h"
#include "UIHelper.h"
#include "xrUICore/Hint/UIHint.h"
#include "GameTask.h"
#include "map_location.h"
#include "map_location_defs.h"
#include "map_manager.h"
#include "UIInventoryUtilities.h"
#include "Level.h"
#include "GametaskManager.h"
#include "Actor.h"
#include "xrUICore/Buttons/UICheckButton.h"

CUITaskWnd::CUITaskWnd(UIHint* hint)
    : CUIWindow("CUITaskWnd"),
      m_background(nullptr), m_background2(nullptr),
      m_center_background(nullptr), m_right_bottom_background(nullptr),
      m_task_split(nullptr), m_pMapWnd(nullptr),
      m_pStoryLineTaskItem(nullptr), m_pSecondaryTaskItem(nullptr),
      m_BtnTaskListWnd(nullptr), m_second_task_index(nullptr),
      m_devider(nullptr), m_actual_frame(0),
      m_btn_focus(nullptr), m_btn_focus2(nullptr),
      m_task_wnd(nullptr), m_task_wnd_show(false),
      m_map_legend_wnd(nullptr), hint_wnd(hint)
{
}

CUITaskWnd::~CUITaskWnd() { delete_data(m_pMapWnd); }

bool CUITaskWnd::Init()
{
    CUIXml xml;
    if (!xml.Load(CONFIG_PATH, UI_PATH, UI_PATH_DEFAULT, PDA_TASK_XML, false))
        return false;

    VERIFY(hint_wnd);

    CUIXmlInit::InitWindow(xml, "main_wnd", 0, this);

    m_background = UIHelper::CreateFrameWindow(xml, "background", this, false);
    m_background2 = UIHelper::CreateFrameLine(xml, "background", this, false);

    m_task_split = UIHelper::CreateFrameLine(xml, "task_split", this, false);

    constexpr std::tuple<eSpotsFilter, pcstr> filters[] =
    {
        { eSpotsFilterTreasures,        "filter_treasures" },
        { eSpotsFilterQuestNpcs,        "filter_primary_objects" },
        { eSpotsFilterSecondaryTasks,   "filter_secondary_tasks" },
        { eSpotsFilterPrimaryObjects,   "filter_quest_npcs" },
    };

    for (const auto& [filter_id, filter_section] : filters)
    {
        auto& filter = m_filters[filter_id];
        filter = UIHelper::CreateCheck(xml, filter_section, this, false);
        if (filter)
        {
            filter->SetCheck(true);
            AddCallback(filter, BUTTON_CLICKED, void_function(this, &CUITaskWnd::OnMapSpotFilterClicked));
        }
        m_filters_state[filter_id] = true;
    }

    m_pMapWnd = xr_new<CUIMapWnd>(hint_wnd);
    m_pMapWnd->SetAutoDelete(false);
    m_pMapWnd->Init(PDA_TASK_XML, "map_wnd");
    AttachChild(m_pMapWnd);

    m_center_background = UIHelper::CreateStatic(xml, "center_background", this);
    m_devider = UIHelper::CreateStatic(xml, "line_devider", this, false);

    m_pStoryLineTaskItem = xr_new<CUITaskItem>();
    m_pStoryLineTaskItem->Init(xml, "storyline_task_item");
    AttachChild(m_pStoryLineTaskItem);
    m_pStoryLineTaskItem->SetAutoDelete(true);
    AddCallback(m_pStoryLineTaskItem, WINDOW_LBUTTON_DB_CLICK,
        CUIWndCallback::void_function(this, &CUITaskWnd::OnTask1DbClicked));

    if (xml.NavigateToNode("secondary_task_item")) // XXX: replace with UIHelper
    {
        Level().GameTaskManager().AllowMultipleTask(true);
        m_pSecondaryTaskItem = xr_new<CUITaskItem>();
        m_pSecondaryTaskItem->Init(xml, "secondary_task_item");
        AttachChild(m_pSecondaryTaskItem);
        m_pSecondaryTaskItem->SetAutoDelete(true);
        AddCallback(m_pSecondaryTaskItem, WINDOW_LBUTTON_DB_CLICK, CUIWndCallback::void_function(this, &CUITaskWnd::OnTask2DbClicked));
    }

    m_btn_focus = UIHelper::Create3tButton(xml, "btn_task_focus", this);
    Register(m_btn_focus);
    AddCallback(m_btn_focus, BUTTON_DOWN, CUIWndCallback::void_function(this, &CUITaskWnd::OnTask1DbClicked));
    // XXX: 3tButtonEx
    //m_btn_focus->set_hint_wnd(hint_wnd);

    m_btn_focus2 = UIHelper::Create3tButton(xml, "btn_task_focus2", this, false);
    if (m_btn_focus2)
    {
        Register(m_btn_focus2);
        AddCallback(m_btn_focus2, BUTTON_DOWN, CUIWndCallback::void_function(this, &CUITaskWnd::OnTask2DbClicked));
        //m_btn_focus2->set_hint_wnd(hint_wnd);
    }

    m_BtnTaskListWnd = UIHelper::Create3tButton(xml, "btn_second_task", this);
    AddCallback(m_BtnTaskListWnd, BUTTON_CLICKED, CUIWndCallback::void_function(this, &CUITaskWnd::OnShowTaskListWnd));

    m_second_task_index = UIHelper::CreateStatic(xml, "second_task_index", this, false);

    m_task_wnd = xr_new<UITaskListWnd>();
    m_task_wnd->SetAutoDelete(true);
    m_task_wnd->hint_wnd = hint_wnd;
    m_task_wnd->init_from_xml(xml, "second_task_wnd");

    m_pMapWnd->AttachChild(m_task_wnd);
    m_task_wnd->SetMessageTarget(this);
    m_task_wnd->Show(false);
    m_task_wnd_show = false;

    m_map_legend_wnd = xr_new<UIMapLegend>();
    m_map_legend_wnd->SetAutoDelete(true);
    m_map_legend_wnd->init_from_xml(xml, "map_legend_wnd");
    m_pMapWnd->AttachChild(m_map_legend_wnd);
    m_map_legend_wnd->SetMessageTarget(this);
    m_map_legend_wnd->Show(false);

    return true;
}

void CUITaskWnd::Update()
{
    if (Level().GameTaskManager().ActualFrame() != m_actual_frame)
    {
        ReloadTaskInfo();
    }

    if (m_pStoryLineTaskItem->show_hint && m_pStoryLineTaskItem->OwnerTask())
    {
        m_pMapWnd->ShowHintTask(m_pStoryLineTaskItem->OwnerTask(), m_pStoryLineTaskItem);
    }
    else if (m_pSecondaryTaskItem && m_pSecondaryTaskItem->show_hint && m_pSecondaryTaskItem->OwnerTask())
    {
        m_pStoryLineTaskItem->show_hint = false;
        m_pMapWnd->ShowHintTask(m_pSecondaryTaskItem->OwnerTask(), m_pSecondaryTaskItem);
    }
    else
    {
        m_pMapWnd->HideCurHint();
    }
    inherited::Update();
}

void CUITaskWnd::Draw() { inherited::Draw(); }
void CUITaskWnd::DrawHint() { m_pMapWnd->DrawHint(); }

void CUITaskWnd::DropFilterSelection()
{
    m_selected_filter = -1;
    GetUICursor().WarpToWindow(nullptr, pInput->IsCurrentInputTypeController());
}

bool CUITaskWnd::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
    if (keyboard_action == WINDOW_KEY_PRESSED)
    {
        if (IsBinded(kPDA_FILTER_TOGGLE, dik, EKeyContext::PDA) && m_selected_filter == -1)
        {
            m_selected_filter = 0;
            GetUICursor().WarpToWindow(m_filters[m_selected_filter]);
            GetUICursor().PauseAutohiding(true);
            return true;
        }

        if (m_selected_filter >= 0)
        {
            if (IsBinded(kQUIT, dik))
            {
                DropFilterSelection();
                return false; // allow PDA to hide
            }

            switch (GetBindedAction(dik, EKeyContext::UI))
            {
            case kUI_BACK:
                DropFilterSelection();
                return true;

            case kUI_ACCEPT:
                m_filters[m_selected_filter]->OnMouseDown(MOUSE_1);
                return true;

            case kUI_MOVE_LEFT:
            case kUI_MOVE_DOWN:
                if (m_selected_filter > 0)
                    m_selected_filter--;
                else
                    m_selected_filter = int(m_filters.size() - 1);
                break;

            case kUI_MOVE_RIGHT:
            case kUI_MOVE_UP:
                if (m_selected_filter < int(m_filters.size() - 1))
                    m_selected_filter++;
                else
                    m_selected_filter = 0;
                break;
            } // switch (GetBindedAction(dik, EKeyContext::UI))

            if (IsBinded(kPDA_FILTER_TOGGLE, dik, EKeyContext::PDA))
            {
                DropFilterSelection();
                return true;
            }

            GetUICursor().WarpToWindow(m_filters[m_selected_filter]);
            return true;
        }
    }

    return inherited::OnKeyboardAction(dik, keyboard_action);
}

bool CUITaskWnd::OnControllerAction(int axis, float x, float y, EUIMessages controller_action)
{
    switch (GetBindedAction(axis, EKeyContext::UI))
    {
    default:
        return OnKeyboardAction(axis, controller_action);
    case kUI_MOVE:
        if (m_selected_filter >= 0)
            return true; // just screw it for now
    }
    return inherited::OnControllerAction(axis, x, y, controller_action);
}

void CUITaskWnd::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
    if (msg == PDA_TASK_SET_TARGET_MAP && pData)
    {
        CGameTask* task = static_cast<CGameTask*>(pData);
        TaskSetTargetMap(task);
        return;
    }
    if (msg == PDA_TASK_SHOW_MAP_SPOT && pData && IsSecondaryTasksEnabled())
    {
        CGameTask* task = static_cast<CGameTask*>(pData);
        TaskShowMapSpot(task, true);
        return;
    }
    if (msg == PDA_TASK_HIDE_MAP_SPOT && pData)
    {
        CGameTask* task = static_cast<CGameTask*>(pData);
        TaskShowMapSpot(task, false);
        return;
    }

    if (msg == PDA_TASK_SHOW_HINT && pData)
    {
        CGameTask* task = static_cast<CGameTask*>(pData);
        m_pMapWnd->ShowHintTask(task, pWnd);
        return;
    }
    if (msg == PDA_TASK_HIDE_HINT)
    {
        m_pMapWnd->HideCurHint();
        return;
    }

    inherited::SendMessage(pWnd, msg, pData);
    CUIWndCallback::OnEvent(pWnd, msg, pData);
}

void CUITaskWnd::ReloadTaskInfo()
{
    CGameTask* storyTask = Level().GameTaskManager().ActiveTask(eTaskTypeStoryline);
    m_pStoryLineTaskItem->InitTask(storyTask);

    CGameTask* additionalTask = nullptr;
    if (m_pSecondaryTaskItem)
    {
        additionalTask = Level().GameTaskManager().ActiveTask(eTaskTypeAdditional);
        m_pSecondaryTaskItem->InitTask(additionalTask);
    }

    if (!storyTask || (storyTask->m_map_object_id == u16(-1) || storyTask->m_map_location.size() == 0))
        m_btn_focus->Show(false);
    else
        m_btn_focus->Show(true);

    if (m_btn_focus2)
    {
        if (!additionalTask || (additionalTask->m_map_object_id == u16(-1) || additionalTask->m_map_location.size() == 0))
            m_btn_focus2->Show(false);
        else
            m_btn_focus2->Show(true);
    }

    vLocations map_locs = Level().MapManager().Locations();
    auto b = map_locs.begin(), e = map_locs.end();
    for (; b != e; ++b)
    {
        shared_str spot = b->spot_type;
        if (strstr(spot.c_str(), "treasure"))
            IsTreasuresEnabled() ? b->location->EnableSpot() : b->location->DisableSpot();
        else if (spot == "primary_object")
            IsPrimaryObjectsEnabled() ? b->location->EnableSpot() : b->location->DisableSpot();
        else if (spot == "secondary_task_location" || spot == "secondary_task_location_complex_timer")
            (/*b->location->SpotEnabled() && */ IsSecondaryTasksEnabled()) ? b->location->EnableSpot() :
                                                                            b->location->DisableSpot();
        else if (spot == "ui_pda2_trader_location" || spot == "ui_pda2_mechanic_location" ||
            spot == "ui_pda2_scout_location" || spot == "ui_pda2_quest_npc_location" ||
            spot == "ui_pda2_medic_location" || spot == "ui_pda2_actor_box_location" ||
            spot == "ui_pda2_actor_sleep_location")
            IsQuestNpcsEnabled() ? b->location->EnableSpot() : b->location->DisableSpot();
    }

    if (storyTask || additionalTask)
    {
        m_actual_frame = Level().GameTaskManager().ActualFrame();
        if (m_task_wnd->IsShown())
            m_task_wnd->UpdateList();
    }

    if (!m_second_task_index)
        return;

    if (storyTask && !additionalTask)
    {
        const auto task_count = Level().GameTaskManager().GetTaskCount(eTaskStateInProgress, eTaskTypeStoryline);
        if (task_count)
        {
            const auto task_index = Level().GameTaskManager().GetTaskIndex(storyTask, eTaskStateInProgress, eTaskTypeStoryline);
            string32 buf;
            xr_sprintf(buf, sizeof(buf), "%d / %d", task_index, task_count);

            m_second_task_index->SetVisible(true);
            m_second_task_index->SetText(buf);
        }
        else
        {
            m_second_task_index->SetVisible(false);
            m_second_task_index->SetText("");
        }
    }

    if (additionalTask)
    {
        const auto task2_count = Level().GameTaskManager().GetTaskCount(eTaskStateInProgress, eTaskTypeAdditional);

        if (task2_count)
        {
            const auto task2_index = Level().GameTaskManager().GetTaskIndex(additionalTask, eTaskStateInProgress, eTaskTypeAdditional);
            string32 buf;
            xr_sprintf(buf, sizeof(buf), "%d / %d", task2_index, task2_count);

            m_second_task_index->SetVisible(true);
            m_second_task_index->SetText(buf);
        }
        else
        {
            m_second_task_index->SetVisible(false);
            m_second_task_index->SetText("");
        }
    }
}

void CUITaskWnd::Show(bool status)
{
    inherited::Show(status);
    m_pMapWnd->Show(status);
    m_pMapWnd->HideCurHint();
    m_map_legend_wnd->Show(false);
    if (status)
    {
        ReloadTaskInfo();
        m_task_wnd->Show(m_task_wnd_show);
    }
    else
    {
        //m_task_wnd_show = false;
        m_task_wnd->Show(false);
    }
}

void CUITaskWnd::Reset() { inherited::Reset(); }
void CUITaskWnd::OnNextTaskClicked() {}
void CUITaskWnd::OnPrevTaskClicked() {}
void CUITaskWnd::OnShowTaskListWnd(CUIWindow* w, void* d)
{
    m_task_wnd_show = !m_task_wnd_show;
    m_task_wnd->Show(!m_task_wnd->IsShown());
}

void CUITaskWnd::Show_TaskListWnd(bool status)
{
    m_task_wnd->Show(status);
    m_task_wnd_show = status;
}

void CUITaskWnd::TaskSetTargetMap(CGameTask* task)
{
    if (!task || !IsSecondaryTasksEnabled())
    {
        return;
    }

    TaskShowMapSpot(task, true);
    CMapLocation* ml = task->LinkedMapLocation();
    if (ml && ml->SpotEnabled())
    {
        ml->CalcPosition();
        m_pMapWnd->SetTargetMap(ml->GetLevelName(), ml->GetPosition(), true);
    }
}

void CUITaskWnd::TaskShowMapSpot(CGameTask* task, bool show) const
{
    if (!task || !IsSecondaryTasksEnabled())
    {
        return;
    }

    CMapLocation* ml = task->LinkedMapLocation();
    if (ml)
    {
        if (show)
        {
            ml->EnableSpot();
            ml->CalcPosition();
            m_pMapWnd->SetTargetMap(ml->GetLevelName(), ml->GetPosition(), true);
        }
        else
        {
            ml->DisableSpot();
        }
    }
}

void CUITaskWnd::OnTask1DbClicked(CUIWindow*, void*)
{
    CGameTask* task = Level().GameTaskManager().ActiveTask(eTaskTypeStoryline);
    TaskSetTargetMap(task);
}

void CUITaskWnd::OnTask2DbClicked(CUIWindow*, void*)
{
    CGameTask* task = Level().GameTaskManager().ActiveTask(eTaskTypeAdditional);
    TaskSetTargetMap(task);
}

void CUITaskWnd::ShowMapLegend(bool status) const { m_map_legend_wnd->Show(status); }
void CUITaskWnd::Switch_ShowMapLegend() const { m_map_legend_wnd->Show(!m_map_legend_wnd->IsShown()); }

void CUITaskWnd::OnMapSpotFilterClicked(CUIWindow* ui, void* d)
{
    for (u32 i = 0; i < eSpotsFilter_Count; ++i)
    {
        if (m_filters[i] == ui)
            m_filters_state[i] = m_filters[i]->GetCheck();
    }
    ReloadTaskInfo();
}

// --------------------------------------------------------------------------------------------------
CUITaskItem::CUITaskItem()
    : CUIWindow("CUITaskItem"),
      m_owner(nullptr), show_hint_can(false), show_hint(false), m_hint_wt(500) {}

void CUITaskItem::Init(CUIXml& uiXml, LPCSTR path)
{
    CUIXmlInit::InitWindow(uiXml, path, 0, this);
    m_hint_wt = uiXml.ReadAttribInt(path, 0, "hint_wt", 500);

    const auto init = [&](pcstr name, bool critical = true)
    {
        string256 buff;
        strconcat(sizeof(buff), buff, path, ":", name);
        m_info[name] = UIHelper::CreateStatic(uiXml, buff, this, critical);
    };

    init("t_icon", false);
    init("t_icon_over", false);
    init("t_caption");

    // If icon exist but icon_over doesn't
    // then just use icon for both cases
    if (!m_info["t_icon_over"])
        m_info["t_icon_over"] = m_info["t_icon"];

    show_hint_can = false;
    show_hint = false;
}

void CUITaskItem::InitTask(CGameTask* task)
{
    m_owner = task;
    CUIStatic* S = m_info["t_icon"];
    if (S)
    {
        if (task)
        {
            S->InitTexture(task->m_icon_texture_name.c_str());
            S->SetStretchTexture(true);
            m_info["t_icon_over"]->Show(true);
        }
        else
        {
            S->TextureOff();
            m_info["t_icon_over"]->Show(false);
        }
    }

    S = m_info["t_caption"];
    S->TextItemControl()->SetTextST((task) ? task->m_Title.c_str() : "");
}

void CUITaskItem::OnFocusReceive()
{
    inherited::OnFocusReceive();
    show_hint_can = true;
    show_hint = false;
}

void CUITaskItem::OnFocusLost()
{
    inherited::OnFocusLost();
    show_hint_can = false;
    show_hint = false;
}

void CUITaskItem::Update()
{
    inherited::Update();
    if (m_owner && m_bCursorOverWindow && show_hint_can)
    {
        if (Device.dwTimeGlobal > (m_dwFocusReceiveTime + m_hint_wt))
        {
            show_hint = true;
            return;
        }
    }
}

void CUITaskItem::OnMouseScroll(float iDirection) {}
bool CUITaskItem::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
    if (inherited::OnMouseAction(x, y, mouse_action))
    {
        // return true;
    }

    switch (mouse_action)
    {
    case WINDOW_LBUTTON_DOWN:
    case WINDOW_RBUTTON_DOWN:
    case BUTTON_DOWN:
        show_hint_can = false;
        show_hint = false;
        break;
    } // switch

    return true;
}

void CUITaskItem::SendMessage(CUIWindow* pWnd, s16 msg, void* pData) { inherited::SendMessage(pWnd, msg, pData); }
