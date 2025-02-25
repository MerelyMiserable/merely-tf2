//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "collection_crafting_panel.h"
#include "cdll_client_int.h"
#include "ienginevgui.h"
#include "econ_item_tools.h"
#include "econ_ui.h"
#include <vgui_controls/AnimationController.h>
#include "clientmode_tf.h"
#include "softline.h"
#include "drawing_panel.h"
#include "tf_item_inventory.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>
#include <c_tf_player.h>


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CCollectionCraftingPanel::CCollectionCraftingPanel(vgui::Panel* parent, CItemModelPanelToolTip* pTooltip)
	: BaseClass(parent, "CollectionCraftingPanel")
	, m_pKVItemPanels(NULL)
	, m_pModelPanel(NULL)
	, m_bWaitingForGCResponse(false)
	, m_bEnvelopeReadyToSend(false)
	, m_pMouseOverTooltip(pTooltip)
	, m_bShowing(false)
	, m_bShowImmediately(false)
{
	ListenForGameEvent("gameui_hidden");

	m_pSelectingItemModelPanel = NULL;

	m_pTradeUpContainer = new EditablePanel(this, "TradeUpContainer");
	m_pInspectPanel = new CTFItemInspectionPanel(this, "NewItemPanel");
	m_pInspectPanel->SetOptions(true, true, false);

	// Create the cosmetic result panel properly
	m_pCosmeticResultItemModelPanel = NULL; // Initialize to NULL
	// We'll find it during ApplySchemeSettings instead of creating it here

	m_pStampPanel = new ImagePanel(this, "Stamp");
	m_pStampButton = new CExButton(this, "ApplyStampButton", "");

	EditablePanel* pPaperContainer = new EditablePanel(m_pTradeUpContainer, "PaperContainer");

	m_pOKButton = new CExButton(pPaperContainer, "OkButton", "");
	m_pNextItemButton = new CExButton(this, "NextItemButton", "");

	m_pDrawingPanel = new CDrawingPanel(this, "drawingpanel");
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CCollectionCraftingPanel::~CCollectionCraftingPanel( void )
{
	if ( m_hSelectionPanel )
	{
		m_hSelectionPanel->MarkForDeletion();
	}

	if ( m_pKVItemPanels )
	{
		m_pKVItemPanels->deleteThis();
	}
}
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::SetItemPanelCount( )
{
	// only do this once
	if ( m_vecItemContainers.Count() != 0 )
		return;

	const int nNumItems = GetInputItemCount();
	const int nNumOutput = GetOutputItemCount();

	EditablePanel* pPaperContainer = dynamic_cast<vgui::EditablePanel*>( m_pTradeUpContainer->FindChildByName( "PaperContainer" ) );
	if ( pPaperContainer )
	{
		m_vecItemContainers.SetCount( nNumItems );
		FOR_EACH_VEC( m_vecItemContainers, i )
		{
			m_vecItemContainers[i] = new EditablePanel( pPaperContainer, "itemcontainer" );
		}

		m_vecOutputItemContainers.SetCount( nNumOutput );
		FOR_EACH_VEC( m_vecOutputItemContainers, i )
		{
			m_vecOutputItemContainers[i] = new EditablePanel( pPaperContainer, "itemcontainer" );
		}
	}
}
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::CreateSelectionPanel()
{
	m_hSelectionPanel = new CCollectionCraftingSelectionPanel( this );
}
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::ApplySchemeSettings(vgui::IScheme* pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);

	LoadControlSettings(GetResFile());

	m_pModelPanel = FindControl<CBaseModelPanel>("ReturnModel");
	if (m_pModelPanel)
	{
		m_pModelPanel->SetLookAtCamera(false);
	}

	if (m_pDrawingPanel)
	{
		m_pDrawingPanel->SetType(DRAWING_PANEL_TYPE_CRAFTING);
	}

	// Find the item name panel in the inspect panel
	m_pItemNamePanel = m_pInspectPanel ? m_pInspectPanel->FindControl<CItemModelPanel>("ItemName") : NULL;

	// Find the cosmetic result panel
	if (!m_pCosmeticResultItemModelPanel)
	{
		// Try to find it in the inspect panel first
		if (m_pInspectPanel)
		{
			m_pCosmeticResultItemModelPanel = m_pInspectPanel->FindControl<CItemModelPanel>("CosmeticResultItemModelPanel");
		}

		// If not found in inspect panel, try to find it directly in this panel
		if (!m_pCosmeticResultItemModelPanel)
		{
			m_pCosmeticResultItemModelPanel = FindControl<CItemModelPanel>("CosmeticResultItemModelPanel");
		}

		// If still not found, create it
		if (!m_pCosmeticResultItemModelPanel && m_pInspectPanel)
		{
			m_pCosmeticResultItemModelPanel = new CItemModelPanel(m_pInspectPanel, "CosmeticResultItemModelPanel");
			m_pCosmeticResultItemModelPanel->SetVisible(true);
			m_pCosmeticResultItemModelPanel->SetZPos(100); // Make sure it's on top
		}
	}

	// Debug output
	DevMsg("ApplySchemeSettings: m_pInspectPanel=%p, m_pItemNamePanel=%p, m_pCosmeticResultItemModelPanel=%p\n",
		m_pInspectPanel, m_pItemNamePanel, m_pCosmeticResultItemModelPanel);

	// Hide the BG image. The crafting panel has a BG already
	if (m_pInspectPanel)
	{
		m_pInspectPanel->SetControlVisible("BGImage", false, true);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::ApplySettings( KeyValues *inResourceData )
{
	BaseClass::ApplySettings( inResourceData );

	KeyValues *pItemKV = inResourceData->FindKey( "ItemContainerKV" );
	if ( pItemKV )
	{
		if ( m_pKVItemPanels )
		{
			m_pKVItemPanels->deleteThis();
		}
		m_pKVItemPanels = new KeyValues("ItemContainerKV");
		pItemKV->CopySubkeys( m_pKVItemPanels );
	}

	m_vecImagePanels.Purge();
	m_vecItemPanels.Purge();

	KeyValues *pBoxTopsKV = inResourceData->FindKey( "BoxTops" );
	if ( pBoxTopsKV )
	{
		m_vecBoxTopNames.Purge();
		FOR_EACH_VALUE( pBoxTopsKV, pValue )
		{
			m_vecBoxTopNames.AddToTail( pValue->GetString() );
		}
	}
	Assert( m_vecBoxTopNames.Count() );

	KeyValues *pStampNames = inResourceData->FindKey( "stampimages" );
	if ( pStampNames )
	{
		m_vecStampNames.Purge();
		FOR_EACH_VALUE( pStampNames, pValue )
		{
			m_vecStampNames.AddToTail( pValue->GetString() );
		}
	}
	Assert( m_vecStampNames.Count() );

	KeyValues *pResulStrings = inResourceData->FindKey( "resultstring" );
	if ( pResulStrings )
	{
		m_vecResultStrings.Purge();
		FOR_EACH_VALUE( pResulStrings, pValue )
		{
			m_vecResultStrings.AddToTail( pValue->GetString() );
		}
	}
	Assert( m_vecResultStrings.Count() );

	KeyValues *pLocalizedPanelNames = inResourceData->FindKey( "localizedpanels" );
	if ( pLocalizedPanelNames )
	{
		m_vecLocalizedPanels.Purge();
		FOR_EACH_TRUE_SUBKEY( pLocalizedPanelNames, pValue )
		{
			m_vecLocalizedPanels.AddToTail( { pValue->GetString( "panelname" ), pValue->GetBool( "show_for_english", false ) } );
		}
	}

	CreateItemPanels();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::PerformLayout()
{
	BaseClass::PerformLayout();

	if ( m_pModelPanel )
	{
		m_pModelPanel->SetMDL( "models/player/items/crafting/mannco_crate_tradeup.mdl" );
	}

	FOR_EACH_VEC( m_vecItemContainers, i )
	{
		m_vecItemContainers[ i ]->SetPos( m_iButtonsStartX + m_iButtonsStepX * ( i % 5 )
										, m_iButtonsStartY + m_iButtonsStepY * ( i / 5 ) );
	}

	FOR_EACH_VEC( m_vecOutputItemContainers, i )
	{
		m_vecOutputItemContainers[i]->SetPos( m_iOutputItemStartX + m_iOutputItemStepX * ( i % 5 )
			, m_iOutputItemStartY + m_iOutputItemStepY * ( i / 5 ) );
	}

	if ( steamapicontext && steamapicontext->SteamApps() )
	{
		char uilanguage[ 64 ];
		uilanguage[0] = 0;
		engine->GetUILanguage( uilanguage, sizeof( uilanguage ) );
		ELanguage language = PchLanguageToELanguage( uilanguage );

		FOR_EACH_VEC( m_vecLocalizedPanels, i )
		{
			bool bShow = language == k_Lang_English && m_vecLocalizedPanels[ i ].m_bShowForEnglish;
			Panel* pPanel = m_pTradeUpContainer->FindChildByName( m_vecLocalizedPanels[ i ].m_strPanel, true );
			if ( pPanel )
			{
				pPanel->SetVisible( bShow );
			}
		}
	}

	UpdateOKButton();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::CreateItemPanels()
{
	SetItemPanelCount();

	m_vecImagePanels.SetCount( m_vecItemContainers.Count() );
	m_vecItemPanels.SetCount( m_vecItemContainers.Count() );

	FOR_EACH_VEC( m_vecItemContainers, i )
	{
		m_vecItemContainers[ i ]->ApplySettings( m_pKVItemPanels );
		m_vecImagePanels[ i ] = m_vecItemContainers[ i ]->FindControl< ImagePanel >( "imagepanel" );
		m_vecItemPanels[ i ] = m_vecItemContainers[ i ]->FindControl< CItemModelPanel >( "itempanel" );
		m_vecItemPanels[ i ]->SetActAsButton( true, true );
		m_vecItemPanels[ i ]->SetTooltip( m_pMouseOverTooltip, "" );

		CExButton* pButton = m_vecItemContainers[ i ]->FindControl< CExButton >( "BackgroundButton" );
		if ( pButton )
		{
			pButton->SetCommand( CFmtStr( "select%d", i ) );
			pButton->AddActionSignalTarget( this );
		}
	}

	m_vecOutputImagePanels.SetCount( m_vecOutputItemContainers.Count() );
	m_vecOutputItemPanels.SetCount( m_vecOutputItemContainers.Count() );

	FOR_EACH_VEC( m_vecOutputItemContainers, i )
	{
		m_vecOutputItemContainers[i]->ApplySettings( m_pKVItemPanels );
		m_vecOutputImagePanels[i] = m_vecOutputItemContainers[i]->FindControl< ImagePanel >( "imagepanel" );
		m_vecOutputItemPanels[i] = m_vecOutputItemContainers[i]->FindControl< CItemModelPanel >( "itempanel" );
		m_vecOutputItemPanels[i]->SetTooltip( m_pMouseOverTooltip, "" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "reloadscheme" ) )
	{
		InvalidateLayout( false, true );
		return;
	}


	if ( FStrEq( "doneselectingitems", command ) )
	{
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, "CollectionCrafting_LetterStart" );	
		m_bEnvelopeReadyToSend = false;

		if ( m_vecStampNames.Count() )
		{
			m_pStampPanel->SetImage( m_vecStampNames[ RandomInt( 0, m_vecStampNames.Count() - 1 ) ] );
		}

		return;
	}
	else if ( FStrEq( "cancel", command ) )
	{
		SetVisible( false );
		return;
	}
	else if ( Q_strnicmp( "select", command, 6 ) == 0 )
	{
		SelectPanel( atoi( command + 6 ) );
		return;
	}
	else if ( FStrEq( "envelopesend", command ) )
	{
		GCSDK::CProtoBufMsg<CMsgCraftCollectionUpgrade> msg( k_EMsgGCCraftCollectionUpgrade );

		// Construct message
		FOR_EACH_VEC( m_vecItemPanels, i )
		{
			 if ( m_vecItemPanels[ i ]->GetItem() == NULL )
				 return;

			msg.Body().add_item_id( m_vecItemPanels[ i ]->GetItem()->GetItemID() );
		}
		 // Send if off
		GCClientSystem()->BSendMessage( msg );

		m_bWaitingForGCResponse = true;
		m_nFoundItemID.Purge();
		m_timerResponse.Start( 5.f );
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, "CollectionCrafting_LetterSend" );	
		return;
	}
	else if ( FStrEq( "placestamp", command ) )
	{
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, "CollectionCrafting_PlaceStamp" );	
		return;
	}
	else if( Q_strnicmp( "playcratesequence", command, 17 ) == 0 )
	{
		m_pModelPanel->SetSequence( atoi( command + 17 ), true );
		return;
	}
	else if( FStrEq( "itemget", command ) )
	{
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, "CollectionCrafting_ItemRecieved" );	
		wchar_t *pszLocalized = NULL;

		if ( m_eEconItemOrigin == kEconItemOrigin_FoundInCrate )
		{
			pszLocalized = g_pVGuiLocalize->Find( "#NewItemMethod_FoundInCrate" );
		}
		else if ( m_vecResultStrings.Count() )
		{
			pszLocalized = g_pVGuiLocalize->Find( m_vecResultStrings[ RandomInt( 0, m_vecResultStrings.Count() - 1 ) ] );
		}
		
		m_pInspectPanel->SetDialogVariable( "resultstring", pszLocalized );
		return;
	}
	else if( Q_strnicmp( "playsound", command, 9 ) == 0 )
	{
		vgui::surface()->PlaySound( command + 10 );
		return;
	}
	else if( FStrEq( "startexplanation1", command ) )
	{
		CExplanationPopup *pPopup = dynamic_cast<CExplanationPopup*>( FindChildByName("StartExplanation") );
		if ( pPopup )
		{
			pPopup->Popup();
		}
		return;
	}
	else if( FStrEq( "startexplanation2", command ) )
	{
		CExplanationPopup *pPopup = dynamic_cast<CExplanationPopup*>( FindChildByName("SigningExplanation") );
		if ( pPopup )
		{
			pPopup->Popup();
		}
		return;
	}
	else if ( FStrEq( "nextitem", command ) )
	{
		if ( m_nFoundItemID.Count() > 1 )
		{
			// Remove head, reset timer to drop next item
			m_nFoundItemID.Remove( 0 );
			m_timerResponse.Start( 5.f );
			m_bShowImmediately = true;
		}
	}
	else if( FStrEq( "reload", command ) )
	{
		g_pVGuiLocalize->ReloadLocalizationFiles();
		InvalidateLayout( false, true );
		SetVisible( true );
		return;
	}

	BaseClass::OnCommand( command );
}

void CCollectionCraftingPanel::SelectPanel( int nPanel )
{
	m_pSelectingItemModelPanel = m_vecItemPanels[ nPanel ];

	CCopyableUtlVector< const CEconItemView* > vecCurrentItems;
	FOR_EACH_VEC( m_vecItemPanels, i )
	{
		if ( m_vecItemPanels[ i ]->GetItem() )
		{
			vecCurrentItems.AddToTail( m_vecItemPanels[ i ]->GetItem() );
		}
	}

	if ( !m_hSelectionPanel )
	{
		CreateSelectionPanel();
		m_hSelectionPanel->SetAutoDelete( false );
	}

	if ( m_hSelectionPanel )
	{
		// Clicked on an item in the crafting area. Open up the selection panel.
		m_hSelectionPanel->SetCorrespondingItems( vecCurrentItems );
		m_hSelectionPanel->ShowDuplicateCounts( true );
		m_hSelectionPanel->ShowPanel( 0, true );
		m_hSelectionPanel->SetCaller( this );
		m_hSelectionPanel->SetZPos( GetZPos() + 1 );
	}
}

void CCollectionCraftingPanel::FireGameEvent( IGameEvent *event )
{
	if ( FStrEq( event->GetName(), "gameui_hidden" ) )
	{
		SetVisible( false );
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::OnItemPanelMousePressed( vgui::Panel *panel )
{
	CItemModelPanel *pItemPanel = dynamic_cast < CItemModelPanel * > ( panel );

	if ( pItemPanel && IsVisible() && !pItemPanel->IsGreyedOut() )
	{
		auto idx = m_vecItemPanels.Find( pItemPanel );
		if ( idx != m_vecItemPanels.InvalidIndex() )
		{
			OnCommand( CFmtStr( "select%d", idx ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::OnSelectionReturned( KeyValues *data )
{
	Assert( m_pSelectingItemModelPanel );

	m_hSelectionPanel->SetVisible( false );

	if ( data && m_pSelectingItemModelPanel )
	{
		uint64 ulIndex = data->GetUint64( "itemindex", INVALID_ITEM_ID );

		CEconItemView* pSelectedItem = InventoryManager()->GetLocalInventory()->GetInventoryItemByItemID( ulIndex );

		if ( pSelectedItem )
		{
			vgui::surface()->PlaySound( "ui/trade_up_apply_sticker.wav" );
		}

		auto idx = m_vecItemPanels.Find( m_pSelectingItemModelPanel );
		SetItem( pSelectedItem, idx );
	}

	m_pSelectingItemModelPanel = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::UpdateOKButton()
{
	bool bOKEnabled = true;
	FOR_EACH_VEC( m_vecItemPanels, i )
	{
		bOKEnabled &= m_vecItemPanels[ i ]->GetItem() != NULL;
	}

	m_pOKButton->SetEnabled( bOKEnabled );

	if ( bOKEnabled )
	{
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( m_pOKButton->GetParent(), "CollectionCrafting_OKBlink" );	
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::SetVisible( bool bVisible )
{
	BaseClass::SetVisible( bVisible );

	if ( bVisible )
	{
		m_pInspectPanel->SetVisible( false );

		EditablePanel* pDimmer = FindControl< EditablePanel >( "Dimmer" );
		if ( pDimmer )
		{
			pDimmer->SetAlpha( 0 );
		}

		EditablePanel* pBG = FindControl< EditablePanel >( "BG" );
		if ( pBG )
		{
			pBG->SetPos( pBG->GetXPos(), GetTall() );
		}

		m_pTradeUpContainer->SetVisible( true );
		m_pTradeUpContainer->SetPos( m_pTradeUpContainer->GetXPos(), -700 );
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, "CollectionCrafting_Intro" );
		vgui::surface()->PlaySound( "ui/trade_up_panel_slide.wav" );

		m_pDrawingPanel->ClearLines( GetLocalPlayerIndex() );
	}
	else
	{
		if ( m_hSelectionPanel )
		{
			m_hSelectionPanel->SetVisible( false );
		}

		if ( m_bShowing )
		{
			EconUI()->SetPreventClosure( false );
		}
		m_bShowing = false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::SOCreated( const CSteamID & steamIDOwner, const GCSDK::CSharedObject *pObject, GCSDK::ESOCacheEvent eEvent )
{
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );
	if ( m_bWaitingForGCResponse )
	{
		if( pObject->GetTypeID() != CEconItem::k_nTypeID )
			return;

		CEconItem *pItem = (CEconItem *)pObject;

		if ( IsUnacknowledged( pItem->GetInventoryToken() ) && ( pItem->GetOrigin() == m_eEconItemOrigin ) )
		{
			//Assert( m_nFoundItemID == INVALID_ITEM_ID );
			//m_bWaitingForGCResponse = false;
			m_nFoundItemID.AddToTail( pItem->GetItemID() );
			CEconItemView* pNewEconItemView = InventoryManager()->GetLocalInventory()->GetInventoryItemByItemID( pItem->GetItemID() );
			if ( pNewEconItemView )
			{
				// Acknowledge the item
				InventoryManager()->AcknowledgeItem( pNewEconItemView, true );
				InventoryManager()->SaveAckFile();
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::Show( CUtlVector< const CEconItemView* >& vecStartingItems )
{
	FOR_EACH_VEC( m_vecItemPanels, i )
	{
		const CEconItemView* pItem = i < vecStartingItems.Count() ? vecStartingItems[ i ] : NULL;
		SetItem( pItem, i );
	}

	m_bShowing = true;
	EconUI()->SetPreventClosure( true );
	SetVisible( true );

	m_eEconItemOrigin = kEconItemOrigin_TradeUp;
}
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::SetWaitingForItem(eEconItemOrigin eOrigin, const CTFItemDefinition* crateDef)
{
	pCrate = crateDef;

	// Clear Panels
	FOR_EACH_VEC(m_vecItemPanels, i)
	{
		SetItem(NULL, i);
	}

	m_bShowing = true;
	EconUI()->SetPreventClosure(true);

	if (m_pInspectPanel)
	{
		m_pInspectPanel->SetVisible(false);
		m_pInspectPanel->SetItemCopy(NULL);
	}

	if (m_pCosmeticResultItemModelPanel)
	{
		m_pCosmeticResultItemModelPanel->SetItem(NULL);
	}

	EditablePanel* pDimmer = FindControl<EditablePanel>("Dimmer");
	if (pDimmer)
	{
		pDimmer->SetAlpha(0);
	}

	EditablePanel* pBG = FindControl<EditablePanel>("BG");
	if (pBG)
	{
		pBG->SetPos(pBG->GetXPos(), GetTall());
	}

	if (m_pTradeUpContainer)
	{
		m_pTradeUpContainer->SetVisible(false);
	}

	// Do not use Derived SetVisible since it does extra animations we do not want here
	BaseClass::SetVisible(true);

	m_eEconItemOrigin = eOrigin;

	m_bWaitingForGCResponse = true;
	m_nFoundItemID.Purge();
	m_timerResponse.Start(5.f);
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence(this, "CollectionCrafting_WaitForItemsOnly");
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::SetItem( const CEconItemView* pItem, int nIndex )
{
	if ( nIndex != m_vecItemPanels.InvalidIndex() )
	{
		m_vecImagePanels[ nIndex ]->SetVisible( pItem != NULL );
		m_vecItemPanels[ nIndex ]->SetVisible( pItem != NULL );
		m_vecItemPanels[ nIndex ]->SetItem( pItem );

		if ( pItem && m_vecBoxTopNames.Count() )
		{
			CUniformRandomStream randomStream;
			randomStream.SetSeed( pItem->GetItemID() );
			m_vecImagePanels[ nIndex ]->SetImage( m_vecBoxTopNames[ randomStream.RandomInt( 0, m_vecBoxTopNames.Count() - 1 ) ] );
		}
	}

	UpdateOKButton();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollectionCraftingPanel::OnThink()
{
	BaseClass::OnThink();
	// When waiting for response and haven't found an item yet
	if (m_bWaitingForGCResponse && m_nFoundItemID.Count() == 0 && pCrate != nullptr)
	{
		// Create a unique item ID
		static const itemid_t LOCAL_ITEM_ID_START = 0x7FF00000;
		static itemid_t s_ulNextItemID = 1;
		itemid_t newItemID = LOCAL_ITEM_ID_START + s_ulNextItemID++;

		// Create a new item instance

		// Add null check for pCrateSchemaDef
		if (!pCrate)
		{
			Msg("Error: Crate schema definition is null\n");
			return;
		}

		const CEconItemCollectionDefinition* Collection = pCrate->GetItemCollectionDefinition();

		// The rest of your original CEconItemView* declaration is a duplicate and can be removed
		CEconItemView* pNewEconItemView = new CEconItemView();

		if (Collection->m_iItemDefs.Count() > 0)
		{
			// Get a random index instead of calling Random() directly
			int randomIndex = rand() % Collection->m_iItemDefs.Count();
			int randomitem = Collection->m_iItemDefs[randomIndex];

			pNewEconItemView->Init(randomitem, 6, 100, true); // Use a known item index
			pNewEconItemView->SetItemOriginOverride(kEconItemOrigin_FoundInCrate);
			pNewEconItemView->SetItemID(newItemID);

			// Add the item to our found items list


			m_nFoundItemID.AddToTail(newItemID);

			CEconItemView* pNewEconItemView = InventoryManager()->GetLocalInventory()->GetInventoryItemByItemID(m_nFoundItemID[0]);
		}
		else
		{
			Msg("Error: No items in collection\n");
			delete pNewEconItemView; // Clean up the allocated object
			return;
		}

		bool bIsInspectableWeapon = false;
		bool bIsWeapon = false;
		const CEconItemDefinition* pItemSchemaDef = GetItemSchema()->GetItemDefinition(pNewEconItemView->GetItemID());

		// Add null check for pItemSchemaDef
		if (!pItemSchemaDef)
		{
			Msg("Error: Item schema definition is null\n");
			delete pNewEconItemView; // Clean up the allocated object
			return;
		}

		// Check if the item is NOT a weapon by looking for specific non-weapon item classes
		if (pItemSchemaDef->GetItemClass() && std::string(pItemSchemaDef->GetItemClass()).find("weapon") != std::string::npos || std::string(pItemSchemaDef->GetItemClass()).find("tool") != std::string::npos || std::string(pItemSchemaDef->GetItemClass()).find("tf_wearable") != std::string::npos) {

			bIsInspectableWeapon = false;
			Msg("Found non-weapon item: %s\n", pItemSchemaDef->GetItemClass());
		}
		else {
			bIsInspectableWeapon = true;
			Msg("Found weapon or Tool.\n");
		}

		// Check if the item is NOT a weapon by looking for specific non-weapon item classes
		if (pItemSchemaDef->GetItemClass() && std::string(pItemSchemaDef->GetItemClass()).find("weapon") != std::string::npos) {

			bIsWeapon = false;
			Msg("Found non-weapon item: %s\n", pItemSchemaDef->GetItemClass());
		}
		else {
			bIsWeapon = true;
			Msg("Found weapon or Tool.\n");
		}

		// Add a 0.5-1% chance of unusual quality

		// Make sure the panels are visible and ready
		if (m_pInspectPanel)
		{
			m_pInspectPanel->SetVisible(true);

			if (bIsInspectableWeapon)
			{

                    float chance = RandomFloat(0.0f, 1.0f);
                    if (chance <= 0.1f && bIsWeapon) {
                        pNewEconItemView->SetItemQuality(11);
                    }

					m_pInspectPanel->SetItemCopy(pNewEconItemView);
					m_pInspectPanel->SetSpecialAttributesOnly(true);  // This is set to true for all items
					m_pCosmeticResultItemModelPanel->SetItem(NULL);
			}
			else
			{
					float chance = RandomFloat(0.0f, 1.0f);
					if (chance >= 0.005f && chance <= 0.01f) {
						pNewEconItemView->SetItemQuality(5);
					}

					m_pCosmeticResultItemModelPanel->SetItem(pNewEconItemView);
					m_pCosmeticResultItemModelPanel->SetNameOnly(false);
					m_pInspectPanel->SetSpecialAttributesOnly(true);
					m_pInspectPanel->SetItemCopy(NULL);
			}

			// Set dialog variable for result string
			wchar_t* pszLocalized = g_pVGuiLocalize->Find("#NewItemMethod_FoundInCrate");
			m_pInspectPanel->SetDialogVariable("resultstring", pszLocalized);
		}

		if (m_pItemNamePanel)
		{
			m_pItemNamePanel->SetItem(pNewEconItemView);
			m_pItemNamePanel->SetVisible(true);
		}

		// Trigger the animation sequence
		OnCommand("itemget");

		// Hide the next item button
		if (m_pNextItemButton)
		{
			m_pNextItemButton->SetVisible(false);
		}
	}
}





//* **************************************************************************************************************************************
// Stat Clock Crafting


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CCraftCommonStatClockPanel::CCraftCommonStatClockPanel( vgui::Panel *parent, CItemModelPanelToolTip* pTooltip )
	: BaseClass( parent, pTooltip )
{

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CCraftCommonStatClockPanel::~CCraftCommonStatClockPanel( void )
{

}
//-----------------------------------------------------------------------------
void CCraftCommonStatClockPanel::Show( CUtlVector< const CEconItemView* >& vecStartingItems )
{
	BaseClass::Show( vecStartingItems );

	// Create output
	static CSchemaItemDefHandle pItemDef_CommonStatClock( "Common Stat Clock" );
	m_outputItem.SetItemDefIndex( pItemDef_CommonStatClock->GetDefinitionIndex() );
	m_outputItem.SetItemQuality( AE_UNIQUE );	// Unique by default
	m_outputItem.SetItemLevel( 0 ); // Hide this?
	m_outputItem.SetItemID( 0 );
	m_outputItem.SetInitialized( true );

	m_vecOutputImagePanels[0]->SetVisible( true );
	m_vecOutputItemPanels[0]->SetVisible( true );
	m_vecOutputItemPanels[0]->SetItem( &m_outputItem );

	if ( m_vecBoxTopNames.Count() )
	{
		CUniformRandomStream randomStream;
		randomStream.SetSeed( 0 );
		m_vecOutputImagePanels[0]->SetImage( m_vecBoxTopNames[randomStream.RandomInt( 0, m_vecBoxTopNames.Count() - 1 )] );
	}
}
//-----------------------------------------------------------------------------
void CCraftCommonStatClockPanel::CreateSelectionPanel()
{
	CStatClockCraftingSelectionPanel *pSelectionPanel = new CStatClockCraftingSelectionPanel( this );
	m_hSelectionPanel = (CCollectionCraftingSelectionPanel*)pSelectionPanel;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCraftCommonStatClockPanel::OnCommand( const char *command )
{
	if ( FStrEq( "envelopesend", command ) )
	{
		GCSDK::CProtoBufMsg<CMsgCraftCommonStatClock> msg( k_EMsgGCCraftCommonStatClock );

		// Find out if the user owns this item or not and place in the proper bucket
		CPlayerInventory *pLocalInv = TFInventoryManager()->GetLocalInventory();
		if ( !pLocalInv )
			return;

		FOR_EACH_VEC( m_vecItemPanels, i )
		{
			if ( m_vecItemPanels[i]->GetItem() == NULL )
				return;

			msg.Body().add_item_id( m_vecItemPanels[i]->GetItem()->GetItemID() );
		}
		// Send if off
		GCClientSystem()->BSendMessage( msg );

		m_bWaitingForGCResponse = true;
		m_nFoundItemID.Purge();
		m_timerResponse.Start( 5.f );
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( this, "CollectionCrafting_LetterSend" );
		return;
	}

	BaseClass::OnCommand( command );
}