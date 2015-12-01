#ifndef RAVEN_TEAM_H
#define RAVEN_TEAM_H
#pragma warning (disable:4786)

#include "misc/utils.h"
#include "misc/Cgdi.h"
#include "Raven_Bot.h"

typedef void (Cgdi::*CgdiPenFunction)();

class Team
{
private:

	//each team has a unique ID
	int         m_ID;

	//this is the next valid ID. Each time a BaseGameEntity is instantiated
	//this value is updated
	static int  m_iNextValidID;

	//this must be called within each constructor to make sure the ID is set
	//correctly. It verifies that the value passed to the method is greater
	//or equal to the next valid ID, before setting the ID and incrementing
	//the next valid ID
	void SetID(int val);

	//
	Raven_Bot* m_pTarget;

	//a pointer to the gdi
	CgdiPenFunction m_pPen;

public:
	Team(int ID, CgdiPenFunction pen);
	virtual ~Team() {};

	CgdiPenFunction GetPen() {
		return m_pPen;
	}
};

#endif