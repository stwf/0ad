function GuiInterface() {}

GuiInterface.prototype.Schema =
	"<a:component type='system'/><empty/>";

GuiInterface.prototype.Serialize = function()
{
	return {};
};

GuiInterface.prototype.Init = function()
{
	this.placementEntity = undefined; // = undefined or [templateName, entityID]
};

GuiInterface.prototype.GetSimulationState = function(player)
{
	var ret = {
		"players": []
	};

	var cmpPlayerMan = Engine.QueryInterface(SYSTEM_ENTITY, IID_PlayerManager);
	var n = cmpPlayerMan.GetNumPlayers();
	for (var i = 0; i < n; ++i)
	{
		var playerEnt = cmpPlayerMan.GetPlayerByID(i);
		var cmpPlayer = Engine.QueryInterface(playerEnt, IID_Player);
		var playerData = {
			"name": cmpPlayer.GetName(),
			"civ": cmpPlayer.GetCiv(),
			"colour": cmpPlayer.GetColour(),
			"popCount": cmpPlayer.GetPopulationCount(),
			"popLimit": cmpPlayer.GetPopulationLimit(),
			"resourceCounts": cmpPlayer.GetResourceCounts()
		};
		ret.players.push(playerData);
	}

	return ret;
};

GuiInterface.prototype.GetEntityState = function(player, ent)
{
	var cmpTempMan = Engine.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager);

	// All units must have a template; if not then it's a nonexistent entity id
	var template = cmpTempMan.GetCurrentTemplateName(ent);
	if (!template)
		return null;

	var ret = {
		"id": ent,
		"template": template
	}

	var cmpPosition = Engine.QueryInterface(ent, IID_Position);
	if (cmpPosition)
	{
		ret.position = cmpPosition.GetPosition();
	}

	var cmpHealth = Engine.QueryInterface(ent, IID_Health);
	if (cmpHealth)
	{
		ret.hitpoints = cmpHealth.GetHitpoints();
		ret.maxHitpoints = cmpHealth.GetMaxHitpoints();
	}

	var cmpAttack = Engine.QueryInterface(ent, IID_Attack);
	if (cmpAttack)
	{
		var type = cmpAttack.GetBestAttack(); // TODO: how should we decide which attack to show?
		ret.attack = cmpAttack.GetAttackStrengths(type);
	}

	var cmpArmour = Engine.QueryInterface(ent, IID_DamageReceiver);
	if (cmpArmour)
	{
		ret.armour = cmpArmour.GetArmourStrengths();
	}

	var cmpBuilder = Engine.QueryInterface(ent, IID_Builder);
	if (cmpBuilder)
	{
		ret.buildEntities = cmpBuilder.GetEntitiesList();
	}

	var cmpTrainingQueue = Engine.QueryInterface(ent, IID_TrainingQueue);
	if (cmpTrainingQueue)
	{
		ret.training = {
			"entities": cmpTrainingQueue.GetEntitiesList(),
			"queue": cmpTrainingQueue.GetQueue(),
		};
	}

	var cmpFoundation = Engine.QueryInterface(ent, IID_Foundation);
	if (cmpFoundation)
	{
		ret.foundation = {
			"progress": cmpFoundation.GetBuildPercentage()
		};
	}

	var cmpOwnership = Engine.QueryInterface(ent, IID_Ownership);
	if (cmpOwnership)
	{
		ret.player = cmpOwnership.GetOwner();
	}

	var cmpResourceSupply = Engine.QueryInterface(ent, IID_ResourceSupply);
	if (cmpResourceSupply)
	{
		ret.resourceSupply = {
			"max": cmpResourceSupply.GetMaxAmount(),
			"amount": cmpResourceSupply.GetCurrentAmount(),
			"type": cmpResourceSupply.GetType()
		};
	}

	var cmpResourceGatherer = Engine.QueryInterface(ent, IID_ResourceGatherer);
	if (cmpResourceGatherer)
	{
		ret.resourceGatherRates = cmpResourceGatherer.GetGatherRates();
	}

	return ret;
};

GuiInterface.prototype.GetTemplateData = function(player, name)
{
	var cmpTempMan = Engine.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager);
	var template = cmpTempMan.GetTemplate(name);

	if (!template)
		return null;

	var ret = {};

	if (template.Identity)
	{
		ret.name = {
			"specific": (template.Identity.SpecificName || template.Identity.GenericName),
			"generic": template.Identity.GenericName
		};
		ret.icon_cell = template.Identity.IconCell;
	}

	if (template.Cost)
	{
		ret.cost = {};
		if (template.Cost.Resources.food) ret.cost.food = +template.Cost.Resources.food;
		if (template.Cost.Resources.wood) ret.cost.wood = +template.Cost.Resources.wood;
		if (template.Cost.Resources.stone) ret.cost.stone = +template.Cost.Resources.stone;
		if (template.Cost.Resources.metal) ret.cost.metal = +template.Cost.Resources.metal;
	}

	return ret;
};

GuiInterface.prototype.SetSelectionHighlight = function(player, cmd)
{
	for each (var ent in cmd.entities)
	{
		var cmpSelectable = Engine.QueryInterface(ent, IID_Selectable);
		if (cmpSelectable)
			cmpSelectable.SetSelectionHighlight(cmd.colour);
	}
};

/**
 * Display the building placement preview.
 * cmd.template is the name of the entity template, or "" to disable the preview.
 * cmd.x, cmd.z, cmd.angle give the location.
 * Returns true if the placement is okay (everything is valid and the entity is not obstructed by others).
 */
GuiInterface.prototype.SetBuildingPlacementPreview = function(player, cmd)
{
	// See if we're changing template
	if (!this.placementEntity || this.placementEntity[0] != cmd.template)
	{
		// Destroy the old preview if there was one
		if (this.placementEntity)
			Engine.DestroyEntity(this.placementEntity[1]);

		// Load the new template
		if (cmd.template == "")
		{
			this.placementEntity = undefined;
		}
		else
		{
			this.placementEntity = [cmd.template, Engine.AddLocalEntity("preview|" + cmd.template)];
		}
	}

	if (this.placementEntity)
	{
		// Move the preview into the right location
		var pos = Engine.QueryInterface(this.placementEntity[1], IID_Position);
		if (pos)
		{
			pos.JumpTo(cmd.x, cmd.z);
			pos.SetYRotation(cmd.angle);
		}

		// Check whether it's obstructed by other entities
		var cmpObstruction = Engine.QueryInterface(this.placementEntity[1], IID_Obstruction);
		var colliding = (cmpObstruction && cmpObstruction.CheckCollisions());

		// Set it to a red shade if this is an obstructed location
		var cmpVisual = Engine.QueryInterface(this.placementEntity[1], IID_Visual);
		if (cmpVisual)
		{
			if (colliding)
				cmpVisual.SetShadingColour(1.4, 0.4, 0.4, 1);
			else
				cmpVisual.SetShadingColour(1, 1, 1, 1);
		}

		if (!colliding)
			return true;
	}

	return false;
};

GuiInterface.prototype.SetPathfinderDebugOverlay = function(player, enabled)
{
	var cmpPathfinder = Engine.QueryInterface(SYSTEM_ENTITY, IID_Pathfinder);
	cmpPathfinder.SetDebugOverlay(enabled);
};

GuiInterface.prototype.SetObstructionDebugOverlay = function(player, enabled)
{
	var cmpObstructionManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_ObstructionManager);
	cmpObstructionManager.SetDebugOverlay(enabled);
};

GuiInterface.prototype.SetMotionDebugOverlay = function(player, data)
{
	for each (var ent in data.entities)
	{
		var cmpUnitMotion = Engine.QueryInterface(ent, IID_UnitMotion);
		if (cmpUnitMotion)
			cmpUnitMotion.SetDebugOverlay(data.enabled);
	}
};

// List the GuiInterface functions that can be safely called by GUI scripts.
// (GUI scripts are non-deterministic and untrusted, so these functions must be
// appropriately careful. They are called with a first argument "player", which is
// trusted and indicates the player associated with the current client; no data should
// be returned unless this player is meant to be able to see it.)
var exposedFunctions = {
	"GetSimulationState": 1,
	"GetEntityState": 1,
	"GetTemplateData": 1,
	"SetSelectionHighlight": 1,
	"SetBuildingPlacementPreview": 1,
	"SetPathfinderDebugOverlay": 1,
	"SetObstructionDebugOverlay": 1,
	"SetMotionDebugOverlay": 1,
};

GuiInterface.prototype.ScriptCall = function(player, name, args)
{
	if (exposedFunctions[name])
		return this[name](player, args);
	else
		throw new Error("Invalid GuiInterface Call name \""+name+"\"");
};

Engine.RegisterComponentType(IID_GuiInterface, "GuiInterface", GuiInterface);
