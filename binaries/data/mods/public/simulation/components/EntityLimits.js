function EntityLimits() {}

EntityLimits.prototype.Schema =
	"<a:help>Specifies per category limits on number of entities (buildings or units) that can be created for each player.</a:help>" +
	"<a:example>" +
		"<Limits>" +
		  "<CivilCentre/>" +
		  "<DefenseTower>25</DefenseTower>" +
		  "<Fortress>10</Fortress>" +
		  "<Wonder>1</Wonder>" +
		  "<Hero>1</Hero>" +
		  "<Special>" +
			"<LimitPerCivCentre>1</LimitPerCivCentre>" +
		  "</Special>" +
		"</Limits>" +
	"</a:example>" +
	"<element name='LimitMultiplier'>" +
		"<ref name='positiveDecimal'/>" +
	"</element>" +
	"<element name='Limits'>" +
		"<zeroOrMore>" +
			"<element a:help='Specifies a category of building/unit on which to apply this limit. See BuildRestrictions/TrainingRestrictions for list of categories.'>" +
				"<anyName />" +
				"<choice>" +
					"<text />" +
					"<element name='LimitPerCivCentre' a:help='Specifies that this limit is per number of civil centres.'>" +
						"<data type='nonNegativeInteger'/>" +
					"</element>" +
				"</choice>" +
			"</element>" +
		"</zeroOrMore>" +
	"</element>";

/*
 *	TODO: Use an inheriting player_{civ}.xml template for civ-specific limits
 */

const TRAINING = "training";
const BUILD = "build";

EntityLimits.prototype.Init = function()
{
	this.limit = {};
	this.count = {};
	for (var category in this.template.Limits)
	{
		this.limit[category] = this.template.Limits[category];
		this.count[category] = 0;
	}
};

EntityLimits.prototype.IncreaseCount = function(category, value)
{
	if (this.count[category] !== undefined)
		this.count[category] += value;
};

EntityLimits.prototype.DecreaseCount = function(category, value)
{
	if (this.count[category] !== undefined)
		this.count[category] -= value;
};

EntityLimits.prototype.IncrementCount = function(category)
{
	this.IncreaseCount(category, 1);
};

EntityLimits.prototype.DecrementCount = function(category)
{
	this.DecreaseCount(category, 1);
};

EntityLimits.prototype.GetLimits = function()
{
	return this.limit;
};

EntityLimits.prototype.GetCounts = function()
{
	return this.count;
};

EntityLimits.prototype.AllowedToCreate = function(limitType, category, count)
{
	// TODO: The UI should reflect this before the user tries to place the building,
	//			since the limits are independent of placement location

	// Allow unspecified categories and those with no limit
	if (this.count[category] === undefined || this.limit[category] === undefined)
		return true;
	
	// Rather than complicating the schema unecessarily, just handle special cases here
	if (this.limit[category].LimitPerCivCentre !== undefined)
	{
		if (this.count[category] >= this.count["CivilCentre"] * this.limit[category].LimitPerCivCentre)
		{
			var cmpPlayer = Engine.QueryInterface(this.entity, IID_Player);
			var notification = {
				"player": cmpPlayer.GetPlayerID(),
				"message": category + " " + limitType + " limit of " +
					this.limit[category].LimitPerCivCentre + " per civil centre reached"
			};
			var cmpGUIInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
			cmpGUIInterface.PushNotification(notification);
			
			return false;
		}
	}
	else if (this.count[category] + count > this.limit[category])
	{
		var cmpPlayer = Engine.QueryInterface(this.entity, IID_Player);
		var notification = {
			"player": cmpPlayer.GetPlayerID(),
			"message": category + " " + limitType + " limit of " + this.limit[category] + " reached"};
		var cmpGUIInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
		cmpGUIInterface.PushNotification(notification);
		
		return false;
	}
	
	return true;
}

EntityLimits.prototype.AllowedToBuild = function(category)
{
	// TODO: The UI should reflect this before the user tries to place the building,
	//			since the limits are independent of placement location

	return this.AllowedToCreate(BUILD, category, 1);
};

EntityLimits.prototype.AllowedToTrain = function(category, count)
{
	return this.AllowedToCreate(TRAINING, category, count);
};

EntityLimits.prototype.OnGlobalOwnershipChanged = function(msg)
{
	// This automatically updates entity counts
	var category = null;
	var cmpBuildRestrictions = Engine.QueryInterface(msg.entity, IID_BuildRestrictions);
	if (cmpBuildRestrictions)
		category = cmpBuildRestrictions.GetCategory();
	var cmpTrainingRestrictions = Engine.QueryInterface(msg.entity, IID_TrainingRestrictions);
	if (cmpTrainingRestrictions)
		category = cmpTrainingRestrictions.GetCategory();
	if (category)
	{
		var playerID = (Engine.QueryInterface(this.entity, IID_Player)).GetPlayerID();
		if (msg.from == playerID)
			this.DecrementCount(category);
		if (msg.to == playerID)
			this.IncrementCount(category);
	}
};

Engine.RegisterComponentType(IID_EntityLimits, "EntityLimits", EntityLimits);
