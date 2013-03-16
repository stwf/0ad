var PlayerID = -1;

function BaseAI(settings)
{
	
	if (!settings)
		return;

	// Make some properties non-enumerable, so they won't be serialised
	Object.defineProperty(this, "_player", {value: settings.player, enumerable: false});
	PlayerID = this._player;
	/*
	 Object.defineProperty(this, "_templates", {value: settings.templates, enumerable: false});
	Object.defineProperty(this, "_derivedTemplates", {value: {}, enumerable: false});

	this._entityMetadata = {};

	this._entityCollections = [];
	this._entityCollectionsByDynProp = {};
	this._entityCollectionsUID = 0;
	 */
	
	this.turn = 0;
}

//Return a simple object (using no classes etc) that will be serialized
//into saved games
BaseAI.prototype.Serialize = function()
{
	return {};
	// TODO: ought to get the AI script subclass to serialize its own state
};

//Called after the constructor when loading a saved game, with 'data' being
//whatever Serialize() returned
BaseAI.prototype.Deserialize = function(data)
{
	/*
	var rawEntities = data._rawEntities;
	this._entityMetadata = data._entityMetadata;
	this._entities = {}
	
	for (var id in rawEntities)
	{
		this._entities[id] = new Entity(this, rawEntities[id]);
	}
*/
	// TODO: ought to get the AI script subclass to deserialize its own state
};

/*BaseAI.prototype.GetTemplate = function(name)
{
	if (this._templates[name])
		return this._templates[name];

	if (this._derivedTemplates[name])
		return this._derivedTemplates[name];

	// If this is a foundation template, construct it automatically
	if (name.substr(0, 11) === "foundation|")
	{
		var base = this.GetTemplate(name.substr(11));

		var foundation = {};
		for (var key in base)
			if (!g_FoundationForbiddenComponents[key])
				foundation[key] = base[key];

		this._derivedTemplates[name] = foundation;
		return foundation;
	}

	error("Tried to retrieve invalid template '"+name+"'");
	return null;
};
*/
BaseAI.prototype.InitWithSharedScript = function(state, sharedAI)
{
	this.accessibility = sharedAI.accessibility;
	this.terrainAnalyzer = sharedAI.terrainAnalyzer;
	this.passabilityClasses = sharedAI.passabilityClasses;
	this.passabilityMap = sharedAI.passabilityMap;

	var gameState = sharedAI.gameState[PlayerID];
	gameState.ai = this;
		
	this.InitShared(gameState, sharedAI);
	
	delete gameState.ai;
}

BaseAI.prototype.HandleMessage = function(state, sharedAI)
{
	/*
	if (!this._entities)
	{
		// Do a (shallow) clone of all the initial entity properties (in order
		// to copy into our own script context and minimise cross-context
		// weirdness)
		this._entities = {};
		for (var id in state.entities)
		{
			this._entities[id] = new Entity(this, state.entities[id]);
		}
	}
	else
	{
		this.ApplyEntitiesDelta(state);
	}
*/
	Engine.ProfileStart("HandleMessage setup");

	this.entities = sharedAI.entities;
	this.events = sharedAI.events;
	this.passabilityClasses = sharedAI.passabilityClasses;
	this.passabilityMap = sharedAI.passabilityMap;
	this.player = this._player;
	this.playerData = sharedAI.playersData[this._player];
	this.templates = sharedAI.templates;
	this.territoryMap = sharedAI.territoryMap;
	this.timeElapsed = sharedAI.timeElapsed;
	this.accessibility = sharedAI.accessibility;
	this.terrainAnalyzer = sharedAI.terrainAnalyzer;
	this.techModifications = sharedAI.techModifications[this._player];

	Engine.ProfileStop();
	
	this.OnUpdate(sharedAI);

	// Clean up temporary properties, so they don't disturb the serializer
	delete this.entities;
	delete this.events;
	delete this.passabilityClasses;
	delete this.passabilityMap;
	delete this.player;
	delete this.playerData;
	delete this.templates;
	delete this.territoryMap;
	delete this.timeElapsed;
};

BaseAI.prototype.ApplyEntitiesDelta = function(state)
{
	Engine.ProfileStart("ApplyEntitiesDelta");

	for each (var evt in state.events)
	{
		if (evt.type == "Create")
		{
			if (! state.entities[evt.msg.entity])
			{
				continue; // Sometimes there are things like foundations which get destroyed too fast
			}
			
			this._entities[evt.msg.entity] = new Entity(this, state.entities[evt.msg.entity]);
			
			// Update all the entity collections since the create operation affects static properties as well as dynamic
			for each (var entCollection in this._entityCollections)
			{
				entCollection.updateEnt(this._entities[evt.msg.entity]);
			}
			
		}
		else if (evt.type == "Destroy")
		{
			if (!this._entities[evt.msg.entity])
			{
				continue;
			}
			// The entity was destroyed but its data may still be useful, so
			// remember the entity and this AI's metadata concerning it
			evt.msg.metadata = (evt.msg.metadata || []);
			evt.msg.entityObj = (evt.msg.entityObj || this._entities[evt.msg.entity]);
			evt.msg.metadata[this._player] = this._entityMetadata[evt.msg.entity];

			for each (var entCol in this._entityCollections)
			{
				entCol.removeEnt(this._entities[evt.msg.entity]);
			}

			delete this._entities[evt.msg.entity];
			delete this._entityMetadata[evt.msg.entity];
		}
		else if (evt.type == "TrainingFinished")
		{
			// Apply metadata stored in training queues, but only if they
			// look like they were added by us
			if (evt.msg.owner === this._player)
			{
				for each (var ent in evt.msg.entities)
				{
					for (key in evt.msg.metadata)
					{
						this.setMetadata(this._entities[ent], key, evt.msg.metadata[key])
					}
				}
			}
		}
	}

	for (var id in state.entities)
	{
		var changes = state.entities[id];

		for (var prop in changes)
		{
			if (prop == "position" || prop == "resourceSupplyAmount") {
				if (this.turn % 10 === 0) {
					this._entities[id]._entity[prop] = changes[prop];
					this.updateEntityCollections(prop, this._entities[id]);
				}
			} else {
				this._entities[id]._entity[prop] = changes[prop];
				this.updateEntityCollections(prop, this._entities[id]);
			}
		}
	}
	Engine.ProfileStop();
};

BaseAI.prototype.OnUpdate = function()
{	// AIs override this function
	// They should do at least this.turn++;
};

BaseAI.prototype.chat = function(message)
{
	Engine.PostCommand({"type": "chat", "message": message});
};
BaseAI.prototype.chatTeam = function(message)
{
	Engine.PostCommand({"type": "chat", "message": "/team " +message});
};
BaseAI.prototype.chatEnemies = function(message)
{
	Engine.PostCommand({"type": "chat", "message": "/enemy " +message});
};

BaseAI.prototype.registerUpdatingEntityCollection = function(entCollection)
{
	entCollection.setUID(this._entityCollectionsUID);
	this._entityCollections.push(entCollection);

	for each (var prop in entCollection.dynamicProperties())
	{
		this._entityCollectionsByDynProp[prop] = this._entityCollectionsByDynProp[prop] || [];
		this._entityCollectionsByDynProp[prop].push(entCollection);
	}

	this._entityCollectionsUID++;
};

BaseAI.prototype.removeUpdatingEntityCollection = function(entCollection)
{
	for (var i in this._entityCollections)
	{
		if (this._entityCollections[i].getUID() === entCollection.getUID())
		{
			this._entityCollections.splice(i, 1);
		}
	}

	for each (var prop in entCollection.dynamicProperties())
	{
		for (var i in this._entityCollectionsByDynProp[prop])
		{
			if (this._entityCollectionsByDynProp[prop][i].getUID() === entCollection.getUID())
			{
				this._entityCollectionsByDynProp[prop].splice(i, 1);
			}
		}
	}
};

BaseAI.prototype.updateEntityCollections = function(property, ent)
{
	if (this._entityCollectionsByDynProp[property] !== undefined)
	{
		for each (var entCollection in this._entityCollectionsByDynProp[property])
		{
			entCollection.updateEnt(ent);
		}	
	}
}

BaseAI.prototype.setMetadata = function(ent, key, value)
{
	var metadata = this._entityMetadata[ent.id()];
	if (!metadata)
		metadata = this._entityMetadata[ent.id()] = {};
	metadata[key] = value;

	this.updateEntityCollections('metadata', ent);
	this.updateEntityCollections('metadata.' + key, ent);
}

BaseAI.prototype.getMetadata = function(ent, key)
{
	var metadata = this._entityMetadata[ent.id()];
	
	if (!metadata || !(key in metadata))
		return undefined;
	return metadata[key];
}
