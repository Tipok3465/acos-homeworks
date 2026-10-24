function fail() { failed = 1; exit 1 }
{
    event = ""; wagon = ""; train = ""; number = ""; result = "";
    for (i = 1; i <= NF; ++i) {
        split($i, pair, "=");
        if (pair[1] == "seq") sequence = pair[2] + 0;
        if (pair[1] == "time_ms") time = pair[2] + 0;
        if (pair[1] == "event") event = pair[2];
        if (pair[1] == "wagon") wagon = pair[2];
        if (pair[1] == "train") train = pair[2];
        if (pair[1] == "wagons") number = pair[2] + 0;
        if (pair[1] == "result") result = pair[2];
    }
    if (sequence != NR) fail();
    if (time < previous_time) fail();
    previous_time = time;
    if (event == "PLAN") plan[wagon] = train;
    if (event == "ARRIVE") arrived[train] = 1;
    if (event == "UNCOUPLE") {
        if (!arrived[train] || state[wagon] != "") fail();
        state[wagon] = "waiting";
    }
    if (event == "ASSIGN") {
        if (state[wagon] != "waiting") fail();
        state[wagon] = "assigned";
    }
    if (event == "START") {
        if (state[wagon] != "assigned") fail();
        state[wagon] = "started";
    }
    if (event == "FINISH") {
        if (state[wagon] != "started") fail();
        state[wagon] = "finished";
    }
    if (event == "PLACE") {
        if (state[wagon] != "finished") fail();
        state[wagon] = "placed";
    }
    if (event == "FORM") {
        if (formed[train] || departed[train] || number <= 0) fail();
        formed[train] = number;
    }
    if (event == "DEPART") {
        if (formed[train] != number || departed[train]) fail();
        taken = 0;
        for (w in plan) if (plan[w] == train && state[w] == "placed") {
            ++taken;
            state[w] = "departed";
        }
        if (taken != number) fail();
        departed[train] = 1;
    }
    if (event == "SUMMARY" && result == "0") {
        for (w in state) if (state[w] != "departed") fail();
        for (w in plan) if (state[w] != "departed") fail();
    }
}
END { if (failed) exit 1 }
