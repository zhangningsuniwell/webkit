/*
 * Copyright (C) 2015, 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "LifecycleCallbackQueue.h"

#if ENABLE(CUSTOM_ELEMENTS)

#include "CustomElementsRegistry.h"
#include "DOMWindow.h"
#include "Document.h"
#include "Element.h"
#include "JSCustomElementInterface.h"
#include "JSDOMBinding.h"
#include <heap/Heap.h>
#include <wtf/Optional.h>
#include <wtf/Ref.h>

namespace WebCore {

class LifecycleQueueItem {
public:
    enum class Type {
        ElementUpgrade,
        Connected,
        Disconnected,
        AttributeChanged,
    };

    LifecycleQueueItem(Type type, Element& element, JSCustomElementInterface& elementInterface)
        : m_type(type)
        , m_element(element)
        , m_interface(elementInterface)
    { }

    LifecycleQueueItem(Element& element, JSCustomElementInterface& elementInterface, const QualifiedName& attributeName, const AtomicString& oldValue, const AtomicString& newValue)
        : m_type(Type::AttributeChanged)
        , m_element(element)
        , m_interface(elementInterface)
        , m_attributeName(attributeName)
        , m_oldValue(oldValue)
        , m_newValue(newValue)
    { }

    void invoke()
    {
        switch (m_type) {
        case Type::ElementUpgrade:
            m_interface->upgradeElement(m_element.get());
            break;
        case Type::Connected:
            m_interface->invokeConnectedCallback(m_element.get());
            break;
        case Type::Disconnected:
            m_interface->invokeDisconnectedCallback(m_element.get());
            break;
        case Type::AttributeChanged:
            ASSERT(m_attributeName);
            m_interface->invokeAttributeChangedCallback(m_element.get(), m_attributeName.value(), m_oldValue, m_newValue);
            break;
        }
    }

private:
    Type m_type;
    Ref<Element> m_element;
    Ref<JSCustomElementInterface> m_interface;
    Optional<QualifiedName> m_attributeName;
    AtomicString m_oldValue;
    AtomicString m_newValue;
};

LifecycleCallbackQueue::LifecycleCallbackQueue()
{ }

LifecycleCallbackQueue::~LifecycleCallbackQueue()
{
    ASSERT(m_items.isEmpty());
}

void LifecycleCallbackQueue::enqueueElementUpgrade(Element& element, JSCustomElementInterface& elementInterface)
{
    if (auto* queue = CustomElementLifecycleProcessingStack::ensureCurrentQueue())
        queue->m_items.append(LifecycleQueueItem(LifecycleQueueItem::Type::ElementUpgrade, element, elementInterface));
}

static JSCustomElementInterface* findInterfaceForCustomElement(Element& element)
{
    ASSERT(element.isCustomElement());
    auto* window = element.document().domWindow();
    if (!window)
        return nullptr;

    auto* registry = window->customElementsRegistry();
    if (!registry)
        return nullptr;

    return registry->findInterface(element.tagQName());
}

void LifecycleCallbackQueue::enqueueConnectedCallbackIfNeeded(Element& element)
{
    auto* elementInterface = findInterfaceForCustomElement(element);
    if (!elementInterface)
        return;

    if (auto* queue = CustomElementLifecycleProcessingStack::ensureCurrentQueue())
        queue->m_items.append({LifecycleQueueItem::Type::Connected, element, *elementInterface});
}

void LifecycleCallbackQueue::enqueueDisconnectedCallbackIfNeeded(Element& element)
{
    auto* elementInterface = findInterfaceForCustomElement(element);
    if (!elementInterface)
        return;

    if (auto* queue = CustomElementLifecycleProcessingStack::ensureCurrentQueue())
        queue->m_items.append({LifecycleQueueItem::Type::Disconnected, element, *elementInterface});
}

void LifecycleCallbackQueue::enqueueAttributeChangedCallbackIfNeeded(Element& element, const QualifiedName& attributeName, const AtomicString& oldValue, const AtomicString& newValue)
{
    auto* elementInterface = findInterfaceForCustomElement(element);
    if (!elementInterface || !elementInterface->observesAttribute(attributeName.localName()))
        return;

    if (auto* queue = CustomElementLifecycleProcessingStack::ensureCurrentQueue())
        queue->m_items.append({element, *elementInterface, attributeName, oldValue, newValue});
}

void LifecycleCallbackQueue::invokeAll()
{
    Vector<LifecycleQueueItem> items;
    items.swap(m_items);
    for (auto& item : items)
        item.invoke();
}

LifecycleCallbackQueue* CustomElementLifecycleProcessingStack::ensureCurrentQueue()
{
    // FIXME: This early exit indicates a bug that some DOM API is missing CEReactions
    if (!s_currentProcessingStack)
        return nullptr;

    auto*& queue = s_currentProcessingStack->m_queue;
    if (!queue) // We use a raw pointer to avoid genearing code to delete it in ~CustomElementLifecycleProcessingStack.
        queue = new LifecycleCallbackQueue;
    return queue;
}

CustomElementLifecycleProcessingStack* CustomElementLifecycleProcessingStack::s_currentProcessingStack = nullptr;

void CustomElementLifecycleProcessingStack::processQueue()
{
    ASSERT(m_queue);
    m_queue->invokeAll();
    delete m_queue;
    m_queue = nullptr;
}

}

#endif
