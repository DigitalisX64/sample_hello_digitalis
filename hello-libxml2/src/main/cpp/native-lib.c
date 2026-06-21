/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// JNI bridge for the libxml2 prefab AAR. The AAR ships the prebuilt arm64-v8a
// libxml2.so + headers but no JNI wrapper, so this small C bridge parses an
// embedded XML document and self-checks the parsed tree, returning a status
// string for the Kotlin shell / StatusTest to assert on. All libxml2 code runs
// under Berberis ARM64->x86_64 translation.

#include <jni.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

// Embedded XML document: a tiny catalog of two <book> elements. The probe
// verifies the document parses and that the tree contents match exactly.
static const char kXmlDoc[] =
    "<?xml version=\"1.0\"?>"
    "<catalog>"
    "<book id=\"bk101\"><title>Digitalis</title><qty>42</qty></book>"
    "<book id=\"bk102\"><title>Berberis</title><qty>7</qty></book>"
    "</catalog>";

// Returns the trimmed text content of the first text-bearing child of node, or
// NULL if none. Caller must xmlFree() the returned buffer.
static xmlChar* child_text(xmlNode* node) {
    if (node == NULL) {
        return NULL;
    }
    return xmlNodeGetContent(node);
}

JNIEXPORT jstring JNICALL
Java_com_example_hellolibxml2_MainActivity_runProbe(JNIEnv* env, jobject thiz) {
    (void)thiz;

    char result[256];
    xmlDoc* doc = NULL;
    xmlChar* id = NULL;
    xmlChar* title = NULL;
    xmlChar* qty = NULL;

    // Pin the parser version that this header was compiled against.
    LIBXML_TEST_VERSION

    result[0] = '\0';

    // 1. Parse the embedded document from memory.
    doc = xmlReadMemory(kXmlDoc, (int)(sizeof(kXmlDoc) - 1), "catalog.xml",
                        NULL, 0);
    if (doc == NULL) {
        strcpy(result, "LIBXML2 FAIL: xmlReadMemory returned NULL");
        goto done;
    }

    // 2. Root element must be <catalog>.
    xmlNode* root = xmlDocGetRootElement(doc);
    if (root == NULL) {
        strcpy(result, "LIBXML2 FAIL: no root element");
        goto done;
    }
    if (root->name == NULL ||
        xmlStrcmp(root->name, (const xmlChar*)"catalog") != 0) {
        strcpy(result, "LIBXML2 FAIL: root element is not <catalog>");
        goto done;
    }

    // 3. Iterate the element children, counting <book> elements and capturing
    //    the first book's id attribute and its <title>/<qty> text.
    int bookCount = 0;
    xmlNode* firstBook = NULL;
    for (xmlNode* cur = root->children; cur != NULL; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrcmp(cur->name, (const xmlChar*)"book") == 0) {
            bookCount++;
            if (firstBook == NULL) {
                firstBook = cur;
            }
        }
    }

    if (bookCount != 2) {
        sprintf(result, "LIBXML2 FAIL: expected 2 <book>, found %d", bookCount);
        goto done;
    }
    if (firstBook == NULL) {
        strcpy(result, "LIBXML2 FAIL: first <book> not found");
        goto done;
    }

    // First book's id attribute.
    id = xmlGetProp(firstBook, (const xmlChar*)"id");
    if (id == NULL || xmlStrcmp(id, (const xmlChar*)"bk101") != 0) {
        sprintf(result, "LIBXML2 FAIL: first book id != bk101 (got '%s')",
                id != NULL ? (const char*)id : "(null)");
        goto done;
    }

    // First book's <title> and <qty> child text.
    xmlNode* titleNode = NULL;
    xmlNode* qtyNode = NULL;
    for (xmlNode* c = firstBook->children; c != NULL; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrcmp(c->name, (const xmlChar*)"title") == 0) {
            titleNode = c;
        } else if (xmlStrcmp(c->name, (const xmlChar*)"qty") == 0) {
            qtyNode = c;
        }
    }

    title = child_text(titleNode);
    if (title == NULL || xmlStrcmp(title, (const xmlChar*)"Digitalis") != 0) {
        sprintf(result, "LIBXML2 FAIL: first book title != Digitalis (got '%s')",
                title != NULL ? (const char*)title : "(null)");
        goto done;
    }

    qty = child_text(qtyNode);
    if (qty == NULL || xmlStrcmp(qty, (const xmlChar*)"42") != 0) {
        sprintf(result, "LIBXML2 FAIL: first book qty != 42 (got '%s')",
                qty != NULL ? (const char*)qty : "(null)");
        goto done;
    }

    // 4. All checks passed.
    strcpy(result,
           "LIBXML2 OK (parsed catalog: 2 books, bk101/Digitalis/42 verified)");

done:
    // 5. Free everything; xmlGetProp/xmlNodeGetContent buffers need xmlFree().
    if (id != NULL) {
        xmlFree(id);
    }
    if (title != NULL) {
        xmlFree(title);
    }
    if (qty != NULL) {
        xmlFree(qty);
    }
    if (doc != NULL) {
        xmlFreeDoc(doc);
    }
    xmlCleanupParser();

    return (*env)->NewStringUTF(env, result);
}
