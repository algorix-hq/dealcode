package dealcode_test

import (
	"encoding/hex"
	"fmt"
	"log"

	dealcode "github.com/algorix-hq/dealcode/go"
)

// Example maps counters to codes and back with the default hex alphabet.
// In production, load the key from your secret manager and never change it
// once codes have been issued.
func Example() {
	key, _ := hex.DecodeString("000102030405060708090a0b0c0d0e0f")
	codec, err := dealcode.New(dealcode.Config{Key: key})
	if err != nil {
		log.Fatal(err)
	}

	code, _ := codec.Encode(1)
	fmt.Println(code)

	n, _ := codec.Decode(code)
	fmt.Println(n)

	// Output:
	// 38fa1e
	// 1
}
