#include "Goal_GetItem.h"
#include "Goal_DodgeSideToSide.h"
#include "../Raven_ObjectEnumerations.h"
#include "../Raven_Bot.h"
#include "../navigation/Raven_PathPlanner.h"

#include "Messaging/Telegram.h"
#include "..\Raven_Messages.h"

#include "Goal_Wander.h"
#include "Goal_FollowPath.h"

#include "debug/DebugConsole.h"


int ItemTypeToGoalType(int gt)
{
	switch (gt)
	{
	case type_health:

		return goal_get_health;

	case type_shotgun:

		return goal_get_shotgun;

	case type_rail_gun:

		return goal_get_railgun;

	case type_rocket_launcher:

		return goal_get_rocket_launcher;

	default: throw std::runtime_error("Goal_GetItem cannot determine item type");

	}//end switch
}

//------------------------------- Activate ------------------------------------
//-----------------------------------------------------------------------------
void Goal_GetItem::Activate()
{
	m_iStatus = active;

	RemoveAllSubgoals();

	m_pGiverTrigger = 0;
	m_bFollowingPath = false;

	//request a path to the item
	m_pOwner->GetPathPlanner()->RequestPathToItem(m_iItemToGet);

	if (m_pOwner->GetTargetSys()->isTargetWithinFOV())
	{
		Vector2D dummy;
		//if the bot has space to strafe then do so
		if (m_pOwner->canStepLeft(dummy) || m_pOwner->canStepRight(dummy))
		{
			AddSubgoal(new Goal_DodgeSideToSide(m_pOwner));
		}
	}

	//if not able to strafe,the bot may have to wait a few update cycles before 
	//a path is calculated so for appearances sake it just wanders
	else
	{
		AddSubgoal(new Goal_Wander(m_pOwner));
	}

}

//-------------------------- Process ------------------------------------------
//-----------------------------------------------------------------------------
int Goal_GetItem::Process()
{
	ActivateIfInactive();

	if (hasItemBeenStolen())
	{
		Terminate();
	}

	else
	{
		if (m_bFollowingPath && m_SubGoals.size() == 1 && m_SubGoals.front()->GetType() == goal_strafe)
		{
			m_SubGoals.front()->Terminate();
			delete m_SubGoals.front();
			m_SubGoals.pop_front();
			debug_con << m_pOwner->ID() << " delete strafe" << "";
			m_iStatus = ProcessSubgoals();
			debug_con << m_pOwner->ID() << m_iStatus << "";
		}
		else
			//process the subgoals
			m_iStatus = ProcessSubgoals();
	}

	return m_iStatus;
}
//---------------------------- HandleMessage ----------------------------------
//-----------------------------------------------------------------------------
bool Goal_GetItem::HandleMessage(const Telegram& msg)
{
	//first, pass the message down the goal hierarchy
	bool bHandled = ForwardMessageToFrontMostSubgoal(msg);

	//if the msg was not handled, test to see if this goal can handle it
	if (bHandled == false)
	{
		switch (msg.Msg)
		{
		case Msg_PathReady:

			//clear any existing goals
			RemoveAllSubgoals();

			AddSubgoal(new Goal_FollowPath(m_pOwner,
				m_pOwner->GetPathPlanner()->GetPath()));
			m_bFollowingPath = true;

			if (m_pOwner->GetTargetSys()->isTargetWithinFOV())
			{
				Vector2D dummy;
				//if the bot has space to strafe then do so
				if (m_pOwner->canStepLeft(dummy) || m_pOwner->canStepRight(dummy))
				{
					AddSubgoal(new Goal_DodgeSideToSide(m_pOwner));
				}
			}

			//get the pointer to the item
			m_pGiverTrigger = static_cast<Raven_Map::TriggerType*>(msg.ExtraInfo);

			return true; //msg handled


		case Msg_NoPathAvailable:

			m_iStatus = failed;

			return true; //msg handled

		default: return false;
		}
	}

	//handled by subgoals
	return true;
}

//---------------------------- hasItemBeenStolen ------------------------------
//
//  returns true if the bot sees that the item it is heading for has been
//  picked up by an opponent
//-----------------------------------------------------------------------------
bool Goal_GetItem::hasItemBeenStolen()const
{
	if (m_pGiverTrigger &&
		!m_pGiverTrigger->isActive() &&
		m_pOwner->hasLOSto(m_pGiverTrigger->Pos()))
	{
		return true;
	}

	return false;
}