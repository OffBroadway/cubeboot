package main

import (
	"debug/elf"
	"flag"
	"fmt"
	"os"
)

var validRelocPrefixes = []string{
	"ntsc10_",
	"ntsc11_",
	"ntsc12_001_",
	"ntsc12_101_",
	"pal10_",
	"pal11_",
	"pal12_",
}

func main() {
	flag.Parse()
	binary := flag.Arg(0)
	if binary == "" {
		fmt.Println("requires elf arg")
		os.Exit(0)
	}

	file, err := os.Open(binary)
	if err != nil {
		panic(fmt.Errorf("load file: %w", err))
	}

	e, err := elf.NewFile(file)
	if err != nil {
		panic(fmt.Errorf("parse ELF: %w", err))
	}

	syms, err := e.Symbols()
	if err != nil {
		panic(fmt.Errorf("load symbols: %w", err))
	}

	globalSymbols := make(map[string]bool)
	for _, s := range syms {
		if int(s.Section) >= len(e.Sections) {
			globalSymbols[s.Name] = true
		}
	}

	for _, s := range syms {
		if s.Name == "" || s.Name == "_patches_end" {
			continue
		}

		if int(s.Section) >= len(e.Sections) {
			continue
		}
		if e.Sections[s.Section].Name != ".reloc" {
			continue
		}

		// check if reloc is valid
		fmt.Println("Checking", "["+s.Name+"]")
		symbolValid := true
		for _, prefix := range validRelocPrefixes {
			target := prefix + s.Name
			if _, ok := globalSymbols[target]; !ok {
				fmt.Println("TARGET NOT FOUND", target)
				symbolValid = false
			}
		}

		if symbolValid {
			fmt.Println("RELOC GOOD")
		}

		fmt.Println("")
	}

	// xx
}
