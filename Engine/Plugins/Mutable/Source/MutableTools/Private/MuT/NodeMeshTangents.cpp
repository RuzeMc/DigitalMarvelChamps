// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshTangents.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeMeshTangentsPrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeMeshTangents::Private::s_type = FNodeType(Node::EType::MeshTangents, NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeMeshTangents )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeMeshPtr NodeMeshTangents::GetSource() const
	{
		return m_pD->m_pSource;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshTangents::SetSource( NodeMesh* p )
	{
		m_pD->m_pSource = p;
	}

}
