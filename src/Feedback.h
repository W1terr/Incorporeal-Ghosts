#pragma once

// The spectral whoosh when an attack passes through a ghost.
namespace Feedback
{
	void PassThrough(RE::Actor* a_ghost);  // any thread; at most one sound per ghost every few tenths of a second
	void Update();                         // main thread: plays what was queued
}
