# Unreal Engine Angelscript
See the [http://angelscript.hazelight.se](http://angelscript.hazelight.se/) website for more documentation.

Join the [UnrealEngine-Angelscript Discord Server](https://discord.gg/39wmC2e) for any questions or comments.

## What is it?
UnrealEngine-Angelscript is a set of engine modifications and a plugin for Unreal Engine 5 that integrates a full-featured scripting language.
It is actively developed by [Hazelight Studios](http://hazelight.se) and was the primary driver of gameplay logic in [It Takes Two](https://www.ea.com/games/it-takes-two).

See [Scripting Introduction](https://angelscript.hazelight.se/getting-started/introduction/) for a guide on getting started with the scripting language.

## Features
### Immediate Hot Reload of script files
See your changes to scripted actors and components reflected immediately in the editor when you hit save.

Non-structural changes can be reloaded *while playing* your game for lightning-fast iteration.

### Debugging support through Visual Studio Code
Debug your script code through the Visual Studio Code extension.
Set breakpoints, inspect variables and step through each line.

### Seamless integration with existing C++ and Blueprint workflows
Angelscript classes can override any BlueprintImplementableEvent you expose from C++,
and can be used seamlessly as base classes for child blueprints.

Use whatever combination of tools fits your workflow best.


## Resources
### Documentation
* [Unreal Engine Angelscript](http://angelscript.hazelight.se/) - Main documentation website for the UE-AS project
* [Script API Documentation](http://angelscript.hazelight.se/api/#CClass:System) - API documentation for properties and functions available to script
### Development
* [Unreal Angelscript Clang-Format](https://marketplace.visualstudio.com/items?itemName=Hazelight.unreal-angelscript-clang-format) - Extension for vscode to use clang-format to format UE-AS scripts
* [GameplayMessageRouter with Angelscript](https://github.com/IncantaUnreal/GameplayMessageRouter) - GameplayMessageRouter plugin from the Lyra sample project with added angelscript bindings