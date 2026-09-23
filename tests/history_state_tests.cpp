#include "../src/model/history_state.hpp"
#include <cassert>

int main() {
    mdl::history_state history;
    assert(!history.dirty());

    const auto first_edit = history.advance();
    assert(history.dirty());

    history.mark_saved();
    assert(!history.dirty());

    const auto second_edit = history.advance();
    assert(history.dirty());

    history.undo(second_edit);
    assert(!history.dirty());

    history.redo(second_edit);
    assert(history.dirty());

    // A new branch after undo gets a new revision identity rather than reusing
    // a stack-depth-derived value that could compare equal to a saved state.
    history.undo(second_edit);
    history.undo(first_edit);
    assert(history.dirty());
    const auto branched_edit = history.advance();
    assert(history.dirty());
    history.undo(branched_edit);
    assert(history.dirty());

    history.reset_clean();
    assert(!history.dirty());
}
