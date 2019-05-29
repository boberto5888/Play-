#pragma once

#include "win32/Window.h"
#include "win32/Splitter.h"
#include "FrameDebuggerTab.h"
#include "../DisAsmVu.h"
#include "../MemoryViewMIPS.h"
#include "../RegViewVu.h"
#include "Vu1Vm.h"
#include "TabHost.h"
#include "GifPacketView.h"

class CVu1ProgramView : public Framework::Win32::CWindow, public IFrameDebuggerTab, public boost::signals2::trackable
{
public:
	CVu1ProgramView(HWND, const RECT&, CVu1Vm&);
	virtual ~CVu1ProgramView() = default;

	void UpdateState(CGSHandler*, CGsPacketMetadata*, DRAWINGKICK_INFO*) override;

	void StepVu1();

protected:
	long OnSize(unsigned int, unsigned int, unsigned int) override;
	LRESULT OnWndProc(unsigned int, WPARAM, LPARAM) override;

private:
	void OnMachineStateChange();
	void OnRunningStateChange();

	void OnMachineStateChangeMsg();
	void OnRunningStateChangeMsg();

	CVu1Vm& m_virtualMachine;
	std::unique_ptr<Framework::Win32::CSplitter> m_mainSplitter;
	std::unique_ptr<Framework::Win32::CSplitter> m_subSplitter;

	std::unique_ptr<CTabHost> m_memoryTab;

	std::unique_ptr<CDisAsmVu> m_disAsm;
	std::unique_ptr<CMemoryViewMIPS> m_memoryView;
	std::unique_ptr<CGifPacketView> m_packetView;
	std::unique_ptr<CRegViewVU> m_regView;

	uint32 m_vuMemPacketAddress;
};
