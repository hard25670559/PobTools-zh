// SimpleGraphic Engine
// (c) David Gowor, 2014
//
// Engine Core Header
//

// ==========
// Interfaces
// ==========

// Engine Core
class core_IMain {
public:
	static core_IMain* GetHandle(sys_IMain* sysHnd);
	static void FreeHandle(core_IMain* hnd);

	core_IConfig*	config = nullptr;
	core_IVideo*	video = nullptr;

	virtual void	Init(int argc, char** argv) = 0;
	virtual void	Frame() = 0;
	virtual void	Shutdown() = 0;
	virtual void	KeyEvent(int key, int type) = 0;

	// Make this engine's interface state the one the interface library acts
	// on. Window callbacks call it before anything that asks the interface a
	// question: which state is current is global to the process, so with
	// several POBs open the answers would otherwise come from whichever was
	// created last (see r_IRenderer::MakeImGuiCurrent).
	virtual void	MakeImGuiCurrent() = 0;
	virtual bool	CanExit() = 0;
};

