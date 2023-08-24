// Copyright 2022, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//
//

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };

template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;
