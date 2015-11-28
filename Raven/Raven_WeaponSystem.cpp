#include "Raven_WeaponSystem.h"
#include "armory/Weapon_RocketLauncher.h"
#include "armory/Weapon_RailGun.h"
#include "armory/Weapon_ShotGun.h"
#include "armory/Weapon_Blaster.h"
#include "Raven_Bot.h"
#include "misc/utils.h"
#include "lua/Raven_Scriptor.h"
#include "Raven_Game.h"
#include "Raven_UserOptions.h"
#include "2D/transformations.h"

#include "debug/DebugConsole.h"


//------------------------- ctor ----------------------------------------------
//-----------------------------------------------------------------------------
Raven_WeaponSystem::Raven_WeaponSystem(Raven_Bot* owner,
                                       double ReactionTime,
                                       double AimAccuracy,
                                       double AimPersistance):m_pOwner(owner),
                                                          m_dReactionTime(ReactionTime),
                                                          m_dAimAccuracy(AimAccuracy),
                                                          m_dAimPersistance(AimPersistance)
{
  Initialize();
  InitializeFuzzyModule();
}

//------------------------- dtor ----------------------------------------------
//-----------------------------------------------------------------------------
Raven_WeaponSystem::~Raven_WeaponSystem()
{
  for (unsigned int w=0; w<m_WeaponMap.size(); ++w)
  {
    delete m_WeaponMap[w];
  }
}

//------------------------------ Initialize -----------------------------------
//
//  initializes the weapons
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::Initialize()
{
  //delete any existing weapons
  WeaponMap::iterator curW;
  for (curW = m_WeaponMap.begin(); curW != m_WeaponMap.end(); ++curW)
  {
    delete curW->second;
  }

  m_WeaponMap.clear();

  //set up the container
  m_pCurrentWeapon = new Blaster(m_pOwner);

  m_WeaponMap[type_blaster]         = m_pCurrentWeapon;
  m_WeaponMap[type_shotgun]         = 0;
  m_WeaponMap[type_rail_gun]        = 0;
  m_WeaponMap[type_rocket_launcher] = 0;
}

//-------------------------  InitializeFuzzyModule ----------------------------
//
//  set up some fuzzy variables and rules
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::InitializeFuzzyModule()
{
	FuzzyVariable& ShootingVariation = m_FuzzyModule.CreateFLV("ShootingVariation");

	FzSet& VarCloser = ShootingVariation.AddLeftShoulderSet("VarCloser", 0, 50, 100);
	FzSet& VarNormal = ShootingVariation.AddTriangularSet("VarNormal", 50, 100, 150);
	FzSet& VarFurther = ShootingVariation.AddRightShoulderSet("VarFurther", 100, 150, 200);

	FuzzyVariable& DistToTarget = m_FuzzyModule.CreateFLV("DistToTarget");

	FzSet& Target_Close = DistToTarget.AddLeftShoulderSet("Target_Close", 0, 25, 150);
	FzSet& Target_Medium = DistToTarget.AddTriangularSet("Target_Medium", 25, 150, 300);
	FzSet& Target_Far = DistToTarget.AddRightShoulderSet("Target_Far", 150, 300, 1000);

	FuzzyVariable& TargetVelocity = m_FuzzyModule.CreateFLV("TargetVelocity");

	FzSet& Velocity_Low = TargetVelocity.AddLeftShoulderSet("Velocity_Low", 0, 25, 50);
	FzSet& Velocity_Medium = TargetVelocity.AddTriangularSet("Velocity_Medium", 25, 50, 75);
	FzSet& Velocity_High = TargetVelocity.AddRightShoulderSet("Velocity_High", 50, 75, 100);

	FuzzyVariable& VisibilityTime = m_FuzzyModule.CreateFLV("VisibilityTime");

	FzSet& Short_Disappear = VisibilityTime.AddLeftShoulderSet("Short_Disappear", 0, 50, 60);
	FzSet& Normal_Disappear = VisibilityTime.AddTriangularSet("Normal_Disappear", 50, 60, 80);
	FzSet& Long_Disappear = VisibilityTime.AddRightShoulderSet("Long_Disappear", 60, 80, 1000);

	m_FuzzyModule.AddRule(FzAND(Target_Close, Velocity_Low), VarCloser);
	m_FuzzyModule.AddRule(FzAND(Target_Close, Velocity_Medium), VarNormal);
	m_FuzzyModule.AddRule(FzAND(Target_Close, Velocity_High), VarNormal);

	m_FuzzyModule.AddRule(FzAND(Target_Medium, Velocity_Low), VarNormal);
	m_FuzzyModule.AddRule(FzAND(Target_Medium, Velocity_Medium, Short_Disappear), VarNormal);
	m_FuzzyModule.AddRule(FzAND(Target_Medium, Velocity_Medium, Normal_Disappear), VarCloser);
	m_FuzzyModule.AddRule(FzAND(Target_Medium, Velocity_Medium, Long_Disappear), VarCloser);
	m_FuzzyModule.AddRule(FzAND(Target_Medium, Velocity_High), VarFurther);

	m_FuzzyModule.AddRule(FzAND(Target_Far, Velocity_Low), VarNormal);
	m_FuzzyModule.AddRule(FzAND(Target_Far, Velocity_Medium), VarFurther);
	m_FuzzyModule.AddRule(FzAND(Target_Far, Velocity_High), VarFurther);

}

double Raven_WeaponSystem::GetShootingVariation()
{
	m_FuzzyModule.Fuzzify("DistToTarget", Vec2DDistance(m_pOwner->Pos(), m_pOwner->GetTargetSys()->GetTarget()->Pos()));
	m_FuzzyModule.Fuzzify("TargetVelocity", m_pOwner->GetTargetSys()->GetTarget()->Velocity().Length() * 100);
	m_FuzzyModule.Fuzzify("VisibilityTime", m_pOwner->GetTargetSys()->GetTimeTargetHasBeenOutOfView() * 100);
	return m_FuzzyModule.DeFuzzify("ShootingVariation", FuzzyModule::max_av);
}

//-------------------------------- SelectWeapon -------------------------------
//
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::SelectWeapon()
{ 
  //if a target is present use fuzzy logic to determine the most desirable 
  //weapon.
  if (m_pOwner->GetTargetSys()->isTargetPresent())
  {
    //calculate the distance to the target
    double DistToTarget = Vec2DDistance(m_pOwner->Pos(), m_pOwner->GetTargetSys()->GetTarget()->Pos());

	//get target health
	double TargetHealth = m_pOwner->GetTargetSys()->GetTarget()->Health();

    //for each weapon in the inventory calculate its desirability given the 
    //current situation. The most desirable weapon is selected
    double BestSoFar = MinDouble;

    WeaponMap::const_iterator curWeap;
    for (curWeap=m_WeaponMap.begin(); curWeap != m_WeaponMap.end(); ++curWeap)
    {
      //grab the desirability of this weapon (desirability is based upon
      //distance to target and ammo remaining)
      if (curWeap->second)
      {
        double score = curWeap->second->GetDesirability(DistToTarget, TargetHealth);

        //if it is the most desirable so far select it
        if (score > BestSoFar)
        {
          BestSoFar = score;

          //place the weapon in the bot's hand.
          m_pCurrentWeapon = curWeap->second;
        }
      }
    }
  }

  else
  {
    m_pCurrentWeapon = m_WeaponMap[type_blaster];
  }
}

//--------------------  AddWeapon ------------------------------------------
//
//  this is called by a weapon affector and will add a weapon of the specified
//  type to the bot's inventory.
//
//  if the bot already has a weapon of this type then only the ammo is added
//-----------------------------------------------------------------------------
void  Raven_WeaponSystem::AddWeapon(unsigned int weapon_type)
{
  //create an instance of this weapon
  Raven_Weapon* w = 0;

  switch(weapon_type)
  {
  case type_rail_gun:

    w = new RailGun(m_pOwner); break;

  case type_shotgun:

    w = new ShotGun(m_pOwner); break;

  case type_rocket_launcher:

    w = new RocketLauncher(m_pOwner); break;

  }//end switch
  

  //if the bot already holds a weapon of this type, just add its ammo
  Raven_Weapon* present = GetWeaponFromInventory(weapon_type);

  if (present)
  {
    present->IncrementRounds(w->NumRoundsRemaining());

    delete w;
  }
  
  //if not already holding, add to inventory
  else
  {
    m_WeaponMap[weapon_type] = w;
  }
}


//------------------------- GetWeaponFromInventory -------------------------------
//
//  returns a pointer to any matching weapon.
//
//  returns a null pointer if the weapon is not present
//-----------------------------------------------------------------------------
Raven_Weapon* Raven_WeaponSystem::GetWeaponFromInventory(int weapon_type)
{
  return m_WeaponMap[weapon_type];
}

//----------------------- ChangeWeapon ----------------------------------------
void Raven_WeaponSystem::ChangeWeapon(unsigned int type)
{
  Raven_Weapon* w = GetWeaponFromInventory(type);

  if (w) m_pCurrentWeapon = w;
}

//--------------------------- TakeAimAndShoot ---------------------------------
//
//  this method aims the bots current weapon at the target (if there is a
//  target) and, if aimed correctly, fires a round
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::TakeAimAndShoot()
{
  //aim the weapon only if the current target is shootable or if it has only
  //very recently gone out of view (this latter condition is to ensure the 
  //weapon is aimed at the target even if it temporarily dodges behind a wall
  //or other cover)
  if (m_pOwner->GetTargetSys()->isTargetShootable() ||
      (m_pOwner->GetTargetSys()->GetTimeTargetHasBeenOutOfView() < 
       m_dAimPersistance) )
  {
    //the position the weapon will be aimed at
    Vector2D AimingPos = m_pOwner->GetTargetBot()->Pos();
	
    //if the current weapon is not an instant hit type gun the target position
    //must be adjusted to take into account the predicted movement of the 
    //target
    if (GetCurrentWeapon()->GetType() == type_rocket_launcher ||
        GetCurrentWeapon()->GetType() == type_blaster)
    {
      AimingPos = PredictFuturePositionOfTarget();

      //if the weapon is aimed correctly, there is line of sight between the
      //bot and the aiming position and it has been in view for a period longer
      //than the bot's reaction time, shoot the weapon
      if ( m_pOwner->RotateFacingTowardPosition(AimingPos) &&
           (m_pOwner->GetTargetSys()->GetTimeTargetHasBeenVisible() >
            m_dReactionTime) &&
           m_pOwner->hasLOSto(AimingPos) )
      {
        AddNoiseToAim(AimingPos);

        GetCurrentWeapon()->ShootAt((GetShootingVariation() / 100) * AimingPos);
      }
    }

    //no need to predict movement, aim directly at target
    else
    {
      //if the weapon is aimed correctly and it has been in view for a period
      //longer than the bot's reaction time, shoot the weapon
      if ( m_pOwner->RotateFacingTowardPosition(AimingPos) &&
           (m_pOwner->GetTargetSys()->GetTimeTargetHasBeenVisible() >
            m_dReactionTime) )
      {
        AddNoiseToAim(AimingPos);
        
        GetCurrentWeapon()->ShootAt(AimingPos);
      }
    }

  }
  
  //no target to shoot at so rotate facing to be parallel with the bot's
  //heading direction
  else
  {
    m_pOwner->RotateFacingTowardPosition(m_pOwner->Pos()+ m_pOwner->Heading());
  }
}

//---------------------------- AddNoiseToAim ----------------------------------
//
//  adds a random deviation to the firing angle not greater than m_dAimAccuracy 
//  rads
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::AddNoiseToAim(Vector2D& AimingPos)const
{
  Vector2D toPos = AimingPos - m_pOwner->Pos();

  Vec2DRotateAroundOrigin(toPos, RandInRange(-m_dAimAccuracy, m_dAimAccuracy));

  AimingPos = toPos + m_pOwner->Pos();
}

//-------------------------- PredictFuturePositionOfTarget --------------------
//
//  predicts where the target will be located in the time it takes for a
//  projectile to reach it. This uses a similar logic to the Pursuit steering
//  behavior.
//-----------------------------------------------------------------------------
Vector2D Raven_WeaponSystem::PredictFuturePositionOfTarget()const
{
  double MaxSpeed = GetCurrentWeapon()->GetMaxProjectileSpeed();
  
  //if the target is ahead and facing the agent shoot at its current pos
  Vector2D ToEnemy = m_pOwner->GetTargetBot()->Pos() - m_pOwner->Pos();
 
  //the lookahead time is proportional to the distance between the enemy
  //and the pursuer; and is inversely proportional to the sum of the
  //agent's velocities
  double LookAheadTime = ToEnemy.Length() / 
                        (MaxSpeed + m_pOwner->GetTargetBot()->MaxSpeed());
  
  //return the predicted future position of the enemy
  return m_pOwner->GetTargetBot()->Pos() + 
         m_pOwner->GetTargetBot()->Velocity() * LookAheadTime;
}


//------------------ GetAmmoRemainingForWeapon --------------------------------
//
//  returns the amount of ammo remaining for the specified weapon. Return zero
//  if the weapon is not present
//-----------------------------------------------------------------------------
int Raven_WeaponSystem::GetAmmoRemainingForWeapon(unsigned int weapon_type)
{
  if (m_WeaponMap[weapon_type])
  {
    return m_WeaponMap[weapon_type]->NumRoundsRemaining();
  }

  return 0;
}

//---------------------------- ShootAt ----------------------------------------
//
//  shoots the current weapon at the given position
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::ShootAt(Vector2D pos)const
{
  GetCurrentWeapon()->ShootAt(pos);
}

//-------------------------- RenderCurrentWeapon ------------------------------
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::RenderCurrentWeapon()const
{
  GetCurrentWeapon()->Render();
}

void Raven_WeaponSystem::RenderDesirabilities()const
{
  Vector2D p = m_pOwner->Pos();

  int num = 0;
  
  WeaponMap::const_iterator curWeap;
  for (curWeap=m_WeaponMap.begin(); curWeap != m_WeaponMap.end(); ++curWeap)
  {
    if (curWeap->second) num++;
  }

  int offset = 15 * num;

    for (curWeap=m_WeaponMap.begin(); curWeap != m_WeaponMap.end(); ++curWeap)
    {
      if (curWeap->second)
      {
        double score = curWeap->second->GetLastDesirabilityScore();
        std::string type = GetNameOfType(curWeap->second->GetType());

        gdi->TextAtPos(p.x+10.0, p.y-offset, ttos(score) + " " + type);

        offset+=15;
      }
    }
}
