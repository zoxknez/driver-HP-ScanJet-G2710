using System.Runtime.CompilerServices;
using System.Windows;

// Testovi vide `internal` clanove. ClassifyExit je detalj implementacije, ali
// mapiranje izlaznog koda u razlog je bas ono sto se mora proveriti - kodovi
// dolaze iz native/cli/main.cpp i lako se raziidju.
[assembly: InternalsVisibleTo("G2710.Qualification.Tests")]

[assembly: ThemeInfo(ResourceDictionaryLocation.None, ResourceDictionaryLocation.SourceAssembly)]
