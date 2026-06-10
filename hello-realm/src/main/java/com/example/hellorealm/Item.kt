package com.example.hellorealm

import io.realm.kotlin.types.RealmObject

/**
 * A minimal Realm model. The `io.realm.kotlin` compiler plugin transforms this
 * RealmObject at compile time (generating the schema accessors that the native
 * librealmc store reads/writes through), so it must be a non-final, var-property
 * class with a public no-arg constructor.
 */
class Item : RealmObject {
    var id: Int = 0
    var name: String = ""
}
