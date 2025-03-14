// Copyright Epic Games, Inc. All Rights Reserved.

import { PerforceContext } from '../common/perforce'

const jsonlint: any = require('jsonlint')

const RESERVED_BRANCH_NAMES = ['NONE', 'DEFAULT', 'IGNORE', 'DEADEND', ''];

export interface BotConfig {
	defaultStreamDepot: string | null
	defaultIntegrationMethod: string | null
	isDefaultBot: boolean
	noStreamAliases: boolean
	globalNotify: string[]
	nagWhenBlocked: boolean | null
	nagSchedule: number[] | null
	nagAcknowledgedSchedule: number[] | null
	nagAcknowledgedLeeway: number | null
	triager: string | null
	checkIntervalSecs: number
	excludeAuthors: string[]
	emailOnBlockage: boolean
	visibility: string[] | string
	slackChannel: string
	reportToBuildHealth: boolean
	mirrorPath: string[]
	aliases: string[] // alternative names for the bot, first in list used for incognito mode
	badgeUrlOverride: string
	branchNamesToIgnore: string[]

	macros: { [name: string]: string[] }
}

const branchBasePrototype = {
	name: '',

	rootPath: '',
	uniqueBranch: false,
	isDefaultBot: false,
	emailOnBlockage: false, // if present, completely overrides BotConfig

	notify: [''],
	flowsTo: [''],
	forceFlowTo: [''],
	defaultFlow: [''],
	macros: {} as { [name: string]: string[] } | undefined,
	resolver: '' as string | null,
	triager: '' as string | null,
	nagWhenBlocked: false as boolean | null,
	nagSchedule: [] as number[] | null,
	nagAcknowledgedSchedule: [] as number[] | null,
	nagAcknowledgedLeeway: 0 as number | null,
	aliases: [''],
	badgeProject: '' as string | null,
}

export type BranchBase = typeof branchBasePrototype

export class IntegrationMethod {
	static NORMAL = 'normal'
	static CONVERT_TO_EDIT = 'convert-to-edit'

	static all() {
		const cls: any = IntegrationMethod
		return Object.getOwnPropertyNames(cls)

			.filter(x => typeof cls[x] === 'string' && cls[x] !== cls.name)
			.map(x => cls[x])
	}
}

export const DAYS_OF_THE_WEEK = ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat']
type DayOfTheWeek = 'sun' | 'mon' | 'tue' | 'wed' | 'thu' | 'fri' | 'sat'

export type IntegrationWindowPane = {
	// if days not specified, daily
	daysOfTheWeek?: DayOfTheWeek[]
	startHourUTC: number
	durationHours: number
}


export const commonOptionFieldsPrototype = {
	lastGoodCLPath: 0 as string | number,
	waitingForCISLink: "",
	pauseCISUnlessAtGate: false,

	initialCL: 0,
	forcePause: false,

	disallowSkip: false,
	incognitoMode: false,

	excludeAuthors: [] as string[], // if present, completely overrides BotConfig

	// by default, specify when gate catch ups are allowed; can be inverted to disallow
	integrationWindow: [] as IntegrationWindowPane[],
	invertIntegrationWindow: false,

	// fake property
	_comment: ''
}

export type CommonOptionFields = typeof commonOptionFieldsPrototype

const nodeOptionFieldsPrototype = {
	...branchBasePrototype,
	...commonOptionFieldsPrototype,

	disabled: false,
	integrationMethod: '',
	forceAll: false,
	visibility: '' as string[] | string,
	blockAssetFlow: [''],
	disallowDeadend: false,

	streamDepot: '',
	streamName: '',
	streamSubpath: '',
	uniqueBranch: false,

	graphNodeColor: '',

	additionalSlackChannelForBlockages: '',
	postMessagesToAdditionalChannelOnly: false,
	ignoreBranchspecs: false,

	badgeUrlOverride: '',
}

const nodeOptionFieldNames: ReadonlySet<string> = new Set(Object.keys(nodeOptionFieldsPrototype));

type NodeOptionFields = typeof nodeOptionFieldsPrototype

// will eventually have all properties listed on wiki
const edgeOptionFieldsPrototype = {
	...commonOptionFieldsPrototype,

	branchspec: '',
	additionalSlackChannel: '',

	postOnlyToAdditionalChannel: false,

	resolver: '',
	triager: '',

	nagSchedule: [],
	nagAcknowledgedSchedule: [],
	nagAcknowledgedLeeway: 0,
	nagWhenBlocked: true,

	terminal: false, // changes go along terminal edges but no further

	implicitCommands: [''],

	ignoreInCycleDetection: false,

	// if set, still generate workspace but use this name
	workspaceNameOverride: '',

	approval: {
		description: '',
		channelId: '',
		block: true
	}
}

const edgeOptionFieldNames: ReadonlySet<string> = new Set(Object.keys(edgeOptionFieldsPrototype));

type EdgeOptionFields = typeof edgeOptionFieldsPrototype
export type ApprovalOptions = typeof edgeOptionFieldsPrototype.approval

export type NodeOptions = Partial<NodeOptionFields>
export type EdgeOptions = Partial<EdgeOptionFields>

export type EdgeProperties = EdgeOptions & {
	from: string
	to: string
}

export interface BranchSpecDefinition {
	from: string
	to: string
	name: string
}

export interface BranchGraphDefinition {
	branches: NodeOptions[]
	branchspecs?: BranchSpecDefinition[] 
	edges?: EdgeProperties[]
}

// for now, just validate branch file data, somewhat duplicating BranchGraph code
// eventually, switch branch map to using a separate class defined here for the branch definitions


function validateCommonOptions(options: Partial<CommonOptionFields>) {
	if (options.integrationWindow) {
		for (const pane of options.integrationWindow) {
			if (pane.daysOfTheWeek) {
				for (let index = 0; index < pane.daysOfTheWeek.length; ++index) { 
					const dayStr = pane.daysOfTheWeek[index]
					const day = dayStr.slice(0, 3).toLowerCase()
					if (DAYS_OF_THE_WEEK.indexOf(day) < 0) {
						throw new Error(`Unknown day of the week ${dayStr}`)
					}
					pane.daysOfTheWeek[index] = day as DayOfTheWeek
				}
			}
		}
	}
}


interface ParseResult {
	branchGraphDef: BranchGraphDefinition | null
	config: BotConfig
}

export type StreamResult = {
	depot: string
	rootPath?: string
	stream?: string
}

/** expects either rootPath or all the other optional arguments */
export function calculateStream(nodeOrStreamName: string, rootPath?: string | null, depot?: string | null, streamSubpath?: string | null) {
	if (!rootPath) {
		if (!depot) {
			throw new Error(`Missing rootPath and no streamDepot defined for branch ${nodeOrStreamName}.`)
		}
		const stream = `//${depot}/${nodeOrStreamName}`
		return {depot, stream, rootPath: stream + (streamSubpath || '/...')}
	}

	if (!rootPath.startsWith('//') || !rootPath.endsWith('/...')) {
		throw new Error(`Branch rootPath not in '//<something>/...'' format: ${rootPath}`)
	}

	const depotMatch = rootPath.match(new RegExp('//([^/]+)/'))
	if (!depotMatch || !depotMatch[1]) {
		throw new Error(`Cannot find depotname in ${rootPath}`)
	}
	return {depot: depotMatch[1]}
}

export class BranchDefs {
	static checkName(name: string) {
		if (!name.match(/^[-a-zA-Z0-9_\.]+$/))
			return `Names must be alphanumeric, dash, underscore or dot: '${name}'`

		for (const reserved of RESERVED_BRANCH_NAMES) {
			if (name.toUpperCase() === reserved) {
				return `'${name}' is a reserved branch name`
			}
		}
		return undefined
	}

	private static checkValidIntegrationMethod(outErrors: string[], method: string, branchName: string) {
		if (IntegrationMethod.all().indexOf(method.toLowerCase()) === -1) {
			outErrors.push(`Unknown integrationMethod '${method}' in '${branchName}'`)
		}
	}

	static async parseAndValidate(p4: PerforceContext, outErrors: string[], branchSpecsText: string, isPreviewing?: boolean): Promise<ParseResult> {
		const defaultConfigForWholeBot: BotConfig = {
			defaultStreamDepot: null,
			defaultIntegrationMethod: null,
			isDefaultBot: false,
			noStreamAliases: false,
			globalNotify: [],
			triager: null,
			nagSchedule: null,
			nagAcknowledgedSchedule: null,
			nagAcknowledgedLeeway: null,
			nagWhenBlocked: null,
			emailOnBlockage: true,
			checkIntervalSecs: 30.0,
			excludeAuthors: [],
			visibility: ['fte'],
			slackChannel: '',
			reportToBuildHealth: false,
			mirrorPath: [],
			aliases: [],
			branchNamesToIgnore: [],
			macros: {},
			badgeUrlOverride: ''
		}

		let branchGraphRaw: any
		try {
			branchGraphRaw = jsonlint.parse(branchSpecsText)

			if (!Array.isArray(branchGraphRaw.branches)) {
				throw new Error('expected "branches" array!')
			}
		}
		catch (err) {
			outErrors.push(err)
			return {branchGraphDef: null, config: defaultConfigForWholeBot}
		}

		if (branchGraphRaw.alias) {
			branchGraphRaw.aliases = [branchGraphRaw.alias, ...(branchGraphRaw.aliases || [])]
		}

		// copy config values
		for (let key of Object.keys(defaultConfigForWholeBot)) {
			let value = branchGraphRaw[key]
			if (value !== undefined) {
				if (key === 'macros') {
					let macrosLower: {[name:string]: string[]} | null = {}
					if (value === null && typeof value !== 'object') {
						macrosLower = null
					}
					else {
						const macrosObj = value as any
						for (const name of Object.keys(macrosObj)) {
							const lines = macrosObj[name]
							if (!Array.isArray(lines)) {
								macrosLower = null
								break
							}
							macrosLower[name.toLowerCase()] = lines
						}
					}
					if (!macrosLower) {
						outErrors.push(`Invalid macro property: '${value}'`)
						return {branchGraphDef: null, config: defaultConfigForWholeBot}
					}
					value = macrosLower
				}
				(defaultConfigForWholeBot as any)[key] = value
			}
		}

		if (defaultConfigForWholeBot.defaultIntegrationMethod) {
			BranchDefs.checkValidIntegrationMethod(outErrors, defaultConfigForWholeBot.defaultIntegrationMethod, 'config')
		}

		const namesToIgnore = defaultConfigForWholeBot.branchNamesToIgnore.map(s => s.toUpperCase())
		defaultConfigForWholeBot.branchNamesToIgnore = namesToIgnore

		const names = new Map<string, string>()

		const branchGraph = branchGraphRaw as BranchGraphDefinition
		const branchesFromJSON = branchGraph.branches
		branchGraph.branches = []

		// Check for duplicate branch names
		for (const def of branchesFromJSON) {
			if (!def.name) {
				outErrors.push(`Unable to parse branch definition: ${JSON.stringify(def)}`)
				continue
			}

			branchGraph.branches.push(def)

			const nameError = BranchDefs.checkName(def.name)
			if (nameError) {
				outErrors.push(nameError)
				continue
			}

			const upperName = def.name.toUpperCase()
			if (names.has(upperName)) {
				outErrors.push(`Duplicate branch name '${upperName}'`)
			}
			else {
				names.set(upperName, upperName)
			}

			const streamResult = calculateStream(
				def.streamName || def.name,
				def.rootPath,
				def.streamDepot || defaultConfigForWholeBot.defaultStreamDepot,
				def.streamSubpath
			)

			if (!isPreviewing && streamResult.stream && !await p4.stream(streamResult.stream)) {
				outErrors.push(`Stream ${streamResult.stream} not found`)
			}
		}

		// Check for duplicate aliases (and that branches/aliases are not in the ignore list)
		const addAlias = (upperBranchName: string, upperAlias: string) => {
			if (namesToIgnore.indexOf(upperBranchName) >= 0) {
				outErrors.push(upperBranchName + ' branch is in branchNamesToIgnore')
			}

			if (namesToIgnore.indexOf(upperAlias) >= 0) {
				outErrors.push(upperAlias + ' alias is in branchNamesToIgnore')
			}

			if (!upperAlias) {
				outErrors.push(`Empty alias for '${upperBranchName}'`)
				return
			}

			const nameError = BranchDefs.checkName(upperAlias)
			if (nameError) {
				outErrors.push(nameError)
				return
			}

			const existing = names.get(upperAlias)
			if (existing && existing !== upperBranchName) {
				outErrors.push(`Duplicate alias '${upperAlias}' for '${existing}' and '${upperBranchName}'`)
			}
			else {
				names.set(upperAlias, upperBranchName)
			}
		}

		for (const def of branchGraph.branches) {

			const upperName = def.name!.toUpperCase()
			if (def.aliases) {
				for (const alias of def.aliases) {
					addAlias(upperName, alias.toUpperCase())
				}
			}

			if (def.streamName && !def.streamSubpath && !defaultConfigForWholeBot.noStreamAliases) {
				addAlias(upperName, def.streamName.toUpperCase())
			}

			if (def.integrationMethod) {
				BranchDefs.checkValidIntegrationMethod(outErrors, def.integrationMethod!, def.name!)
			}

			// check all properties are known (could make things case insensitive here)
			for (const keyName of Object.keys(def)) {
				if (!nodeOptionFieldNames.has(keyName)) {
					throw new Error(`Unknown property '${keyName}' specified for node ${def.name}`)
				}
			}

			validateCommonOptions(def)
		}

		// Check edge properties
		if (branchGraph.edges) {
			for (const edge of branchGraph.edges) {
				if (!names.get(edge.from.toUpperCase())) {
					outErrors.push('Unrecognised source node in edge property ' + edge.from)
				}
				if (!names.get(edge.to.toUpperCase())) {
					outErrors.push('Unrecognised target node in edge property ' + edge.to)
				}

				// check all properties are known (could make things case insensitive here)
				for (const keyName of Object.keys(edge)) {
					if (keyName !== 'from' && keyName !== 'to' && !edgeOptionFieldNames.has(keyName)) {
						throw new Error(`Unknown property '${keyName}' specified for edge ${edge.from}->${edge.to}`)
					}
				}

				if (edge.approval) {
					// should be replaced by generic handling of objects in the prototype
					if (!edge.approval.description || !edge.approval.channelId) {
						throw new Error(`Invalid approval settings for edge ${edge.from}->${edge.to}`)
					}
				}

				validateCommonOptions(edge)
			}
		}

		// Check flow
		for (const def of branchGraph.branches) {
			const flowsTo = new Set()
			if (def.flowsTo) {
				if (!Array.isArray(def.flowsTo)) {
					outErrors.push(`'${def.name}'.flowsTo is not an array`)
				}
				else for (const to of def.flowsTo) {
					const branchName = names.get(to.toUpperCase())
					if (branchName) {
						flowsTo.add(branchName)
					}
					else {
						outErrors.push(`'${def.name}' flows to unknown branch/alias '${to}'`)
					}
				}
			}

			if (def.forceFlowTo) {
				if (!Array.isArray(def.forceFlowTo)) {
					outErrors.push(`'${def.name}'.forceFlowTo is not an array`)
				}
				else for (const to of def.forceFlowTo) {
					const branchName = names.get(to.toUpperCase())
					if (!branchName) {
						outErrors.push(`'${def.name}' force flows to unknown branch/alias '${to}'`)
					}
					else if (!flowsTo.has(branchName)) {
						outErrors.push(`'${def.name}' force flows but does not flow to '${to}'`)
					}
				}
			}
		}

		// Check branchspecs for valid branches
		if (branchGraph.branchspecs) {
			for (const spec of branchGraph.branchspecs) {
				for (const [key, val] of Object.entries(spec)) {
					if (key !== 'from' && key !== 'to' && key !== 'name') {
						outErrors.push('Unexpected branchspec property: ' + key)
					}
					if (typeof val !== 'string') {
						outErrors.push(`Branchspec property ${key} is not a string`)
					}
				}
				if (!spec.from || !spec.to) {
					outErrors.push(`Invalid branchspec ${spec.name} (requires both to and from fields)`)
				}

				if (!names.has(spec.from.toUpperCase())) {
					outErrors.push(`From-Branch ${spec.from} not found in branchspec ${spec.name}`)
				}

				if (!names.has(spec.to.toUpperCase())) {
					outErrors.push(`To-Branch ${spec.to} not found in branchspec ${spec.name}`)
				}
			}
		}

		if (outErrors.length > 0) {
			console.log(outErrors)
			return {branchGraphDef: null, config: defaultConfigForWholeBot}
		}

		return {branchGraphDef: branchGraph, config: defaultConfigForWholeBot}
	}
}
